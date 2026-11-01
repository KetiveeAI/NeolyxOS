/*
 * nxlibc host test: calendar conversion (gmtime, mktime), strftime,
 * asctime/ctime, clock and time. The expected calendar values (dates,
 * weekdays, ISO weeks, strftime text) were produced with Python's
 * datetime module, an independent implementation of the proleptic
 * Gregorian calendar, when the test was written.
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <stdint.h>
#include "libc_test.h"

void nxc_host_rtc_broken(int on);   /* host shim test hook (libc_host_sys.c) */

typedef struct {
    long long t;
    int year, mon, mday, hour, min, sec, wday, yday, iso_year, iso_week;
} cal_case_t;

static const cal_case_t cal_cases[] = {
    { 0LL, 1970, 1, 1, 0, 0, 0, 4, 0, 1970, 1 },
    { -1LL, 1969, 12, 31, 23, 59, 59, 3, 364, 1970, 1 },
    { 86399LL, 1970, 1, 1, 23, 59, 59, 4, 0, 1970, 1 },
    { 86400LL, 1970, 1, 2, 0, 0, 0, 5, 1, 1970, 1 },
    { 951782400LL, 2000, 2, 29, 0, 0, 0, 2, 59, 2000, 9 },
    { 951868800LL, 2000, 3, 1, 0, 0, 0, 3, 60, 2000, 9 },
    { 4107542400LL, 2100, 3, 1, 0, 0, 0, 1, 59, 2100, 9 },
    { 4102444800LL, 2100, 1, 1, 0, 0, 0, 5, 0, 2099, 53 },
    { 253402300799LL, 9999, 12, 31, 23, 59, 59, 5, 364, 9999, 52 },
    { -62135596800LL, 1, 1, 1, 0, 0, 0, 1, 0, 1, 1 },
    { 1700000000LL, 2023, 11, 14, 22, 13, 20, 2, 317, 2023, 46 },
    { 2147483647LL, 2038, 1, 19, 3, 14, 7, 2, 18, 2038, 3 },
    { 2147483648LL, 2038, 1, 19, 3, 14, 8, 2, 18, 2038, 3 },
    { -2208988800LL, 1900, 1, 1, 0, 0, 0, 1, 0, 1900, 1 },
    { 978307199LL, 2000, 12, 31, 23, 59, 59, 0, 365, 2000, 52 },
    { 1709164800LL, 2024, 2, 29, 0, 0, 0, 4, 59, 2024, 9 },
    { -11728417509LL, 1598, 5, 5, 10, 14, 51, 2, 124, 1598, 19 },
    { -30378253193LL, 1007, 5, 9, 20, 20, 7, 6, 128, 1007, 19 },
    { -37675080782LL, 776, 2, 15, 19, 46, 58, 0, 45, 776, 7 },
    { 89129637883LL, 4794, 5, 29, 0, 44, 43, 0, 148, 4794, 21 },
    { 7998428110LL, 2223, 6, 18, 9, 35, 10, 3, 168, 2223, 25 },
    { 183287720802LL, 7778, 2, 26, 15, 46, 42, 4, 56, 7778, 9 },
    { 246949289355LL, 9795, 7, 6, 8, 49, 15, 1, 186, 9795, 28 },
    { 207748525030LL, 8553, 4, 15, 19, 37, 10, 0, 104, 8553, 15 },
    { 11269590408LL, 2327, 2, 14, 1, 46, 48, 1, 44, 2327, 7 },
    { 131143723338LL, 6125, 10, 12, 4, 2, 18, 5, 284, 6125, 41 },
    { 204265595072LL, 8442, 12, 2, 3, 4, 32, 2, 335, 8442, 49 },
    { -4634638345LL, 1823, 2, 19, 8, 27, 35, 3, 49, 1823, 8 },
    { -60005400164LL, 68, 7, 3, 1, 17, 16, 2, 184, 68, 27 },
    { 40628837097LL, 3257, 6, 23, 4, 4, 57, 6, 173, 3257, 25 },
    { 74688846725LL, 4336, 10, 18, 5, 52, 5, 0, 291, 4336, 42 },
    { 106154190223LL, 5333, 11, 22, 11, 3, 43, 0, 325, 5333, 47 },
    { 23889054549LL, 2727, 1, 6, 16, 29, 9, 4, 5, 2727, 1 },
    { 69996690508LL, 4188, 2, 8, 21, 8, 28, 5, 38, 4188, 6 },
    { 102846124760LL, 5229, 1, 23, 15, 59, 20, 2, 22, 5229, 4 },
    { 72472631262LL, 4266, 7, 26, 14, 27, 42, 4, 206, 4266, 30 },
    { 164918451876LL, 7196, 1, 22, 9, 4, 36, 1, 21, 7196, 4 },
    { -34150900676LL, 887, 10, 19, 22, 42, 4, 0, 291, 887, 42 },
    { -29489393515LL, 1035, 7, 9, 13, 48, 5, 4, 189, 1035, 28 },
    { 180302809518LL, 7683, 7, 27, 1, 45, 18, 2, 207, 7683, 30 },
    { 118939273004LL, 5739, 1, 13, 22, 56, 44, 2, 12, 5739, 3 },
    { 206943845556LL, 8527, 10, 16, 9, 32, 36, 4, 288, 8527, 42 },
    { 40081053892LL, 3240, 2, 13, 2, 4, 52, 1, 43, 3240, 7 },
    { -4336360962LL, 1832, 8, 2, 15, 17, 18, 4, 214, 1832, 31 },
    { 207558248806LL, 8547, 4, 5, 13, 6, 46, 3, 94, 8547, 14 },
    { 233156617965LL, 9358, 6, 9, 23, 12, 45, 5, 159, 9358, 23 },
    { 51553180051LL, 3603, 8, 28, 7, 47, 31, 4, 239, 3603, 35 },
    { 193344934788LL, 8096, 11, 8, 14, 19, 48, 4, 312, 8096, 45 },
    { -42694520217LL, 617, 1, 24, 11, 3, 3, 5, 23, 617, 4 },
    { 96233409988LL, 5019, 7, 8, 14, 46, 28, 4, 188, 5019, 27 },
    { -30610559739LL, 999, 12, 28, 2, 44, 21, 6, 361, 999, 52 },
    { 37174244492LL, 3148, 1, 3, 11, 1, 32, 6, 2, 3148, 1 },
    { 102157305019LL, 5207, 3, 28, 5, 10, 19, 3, 86, 5207, 13 },
    { -56451042378LL, 181, 2, 19, 10, 53, 42, 1, 49, 181, 8 },
    { 103496178023LL, 5249, 8, 30, 10, 20, 23, 1, 241, 5249, 35 },
    { 197047130238LL, 8214, 3, 6, 1, 57, 18, 0, 64, 8214, 9 },
};

typedef struct { long long t; const char *fmt; const char *want; } strf_case_t;

static const strf_case_t strf_cases[] = {
    { 1709164800LL, "%a %A %b %B %h", "Thu Thursday Feb February Feb" },
    { 1709164800LL, "%c", "Thu Feb 29 00:00:00 2024" },
    { 1709164800LL, "%C %y %Y %G %g", "20 24 2024 2024 24" },
    { 1709164800LL, "%d %e %j", "29 29 060" },
    { 1709164800LL, "%D %F %T %R", "02/29/24 2024-02-29 00:00:00 00:00" },
    { 1709164800LL, "%H %I %M %S %p", "00 12 00 00 AM" },
    { 1709164800LL, "%m %u %w", "02 4 4" },
    { 1709164800LL, "%U %W %V", "08 09 09" },
    { 1709164800LL, "%x %X", "02/29/24 00:00:00" },
    { 1709164800LL, "%%|%n|%t|", "%|\n|\t|" },
    { 1709164800LL, "%z %Z", "+0000 UTC" },
    { 0LL, "%a %A %b %B %h", "Thu Thursday Jan January Jan" },
    { 0LL, "%c", "Thu Jan  1 00:00:00 1970" },
    { 0LL, "%C %y %Y %G %g", "19 70 1970 1970 70" },
    { 0LL, "%d %e %j", "01  1 001" },
    { 0LL, "%D %F %T %R", "01/01/70 1970-01-01 00:00:00 00:00" },
    { 0LL, "%H %I %M %S %p", "00 12 00 00 AM" },
    { 0LL, "%m %u %w", "01 4 4" },
    { 0LL, "%U %W %V", "00 00 01" },
    { 0LL, "%x %X", "01/01/70 00:00:00" },
    { 0LL, "%%|%n|%t|", "%|\n|\t|" },
    { 0LL, "%z %Z", "+0000 UTC" },
    { 1704067199LL, "%a %A %b %B %h", "Sun Sunday Dec December Dec" },
    { 1704067199LL, "%c", "Sun Dec 31 23:59:59 2023" },
    { 1704067199LL, "%C %y %Y %G %g", "20 23 2023 2023 23" },
    { 1704067199LL, "%d %e %j", "31 31 365" },
    { 1704067199LL, "%D %F %T %R", "12/31/23 2023-12-31 23:59:59 23:59" },
    { 1704067199LL, "%H %I %M %S %p", "23 11 59 59 PM" },
    { 1704067199LL, "%m %u %w", "12 7 0" },
    { 1704067199LL, "%U %W %V", "53 52 52" },
    { 1704067199LL, "%x %X", "12/31/23 23:59:59" },
    { 1704067199LL, "%%|%n|%t|", "%|\n|\t|" },
    { 1704067199LL, "%z %Z", "+0000 UTC" },
    { 1735650309LL, "%a %A %b %B %h", "Tue Tuesday Dec December Dec" },
    { 1735650309LL, "%c", "Tue Dec 31 13:05:09 2024" },
    { 1735650309LL, "%C %y %Y %G %g", "20 24 2024 2025 25" },
    { 1735650309LL, "%d %e %j", "31 31 366" },
    { 1735650309LL, "%D %F %T %R", "12/31/24 2024-12-31 13:05:09 13:05" },
    { 1735650309LL, "%H %I %M %S %p", "13 01 05 09 PM" },
    { 1735650309LL, "%m %u %w", "12 2 2" },
    { 1735650309LL, "%U %W %V", "52 53 01" },
    { 1735650309LL, "%x %X", "12/31/24 13:05:09" },
    { 1735650309LL, "%%|%n|%t|", "%|\n|\t|" },
    { 1735650309LL, "%z %Z", "+0000 UTC" },
    { -2208988800LL, "%a %A %b %B %h", "Mon Monday Jan January Jan" },
    { -2208988800LL, "%c", "Mon Jan  1 00:00:00 1900" },
    { -2208988800LL, "%C %y %Y %G %g", "19 00 1900 1900 00" },
    { -2208988800LL, "%d %e %j", "01  1 001" },
    { -2208988800LL, "%D %F %T %R", "01/01/00 1900-01-01 00:00:00 00:00" },
    { -2208988800LL, "%H %I %M %S %p", "00 12 00 00 AM" },
    { -2208988800LL, "%m %u %w", "01 1 1" },
    { -2208988800LL, "%U %W %V", "00 01 01" },
    { -2208988800LL, "%x %X", "01/01/00 00:00:00" },
    { -2208988800LL, "%%|%n|%t|", "%|\n|\t|" },
    { -2208988800LL, "%z %Z", "+0000 UTC" },
};

static void test_gmtime(void) {
    for (size_t i = 0; i < sizeof cal_cases / sizeof cal_cases[0]; i++) {
        const cal_case_t *c = &cal_cases[i];
        time_t t = (time_t)c->t;
        struct tm tm;
        t_count++;
        if (!gmtime_r(&t, &tm)) {
            t_fail++;
            printf("  FAIL gmtime_r(%lld) returned NULL\n", c->t);
            continue;
        }
        int ok = tm.tm_year + 1900 == c->year && tm.tm_mon + 1 == c->mon && tm.tm_mday == c->mday &&
                 tm.tm_hour == c->hour && tm.tm_min == c->min && tm.tm_sec == c->sec &&
                 tm.tm_wday == c->wday && tm.tm_yday == c->yday && tm.tm_isdst == 0;
        if (!ok) {
            t_fail++;
            printf("  FAIL gmtime(%lld) = %d-%02d-%02d %02d:%02d:%02d wday %d yday %d\n", c->t,
                   tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec,
                   tm.tm_wday, tm.tm_yday);
        }
        /* the way back */
        struct tm copy = tm;
        CHECK_INT(mktime(&copy), c->t);
        /* ISO 8601 week */
        char buf[32];
        strftime(buf, sizeof buf, "%G-%V", &tm);
        char want[32];
        snprintf(want, sizeof want, "%04d-%02d", c->iso_year, c->iso_week);  /* nxlibc pads years to 4 digits (ISO 8601) */
        CHECK_STR(buf, want);
    }
}

static void test_mktime_normalise(void) {
    struct tm tm;
    memset(&tm, 0, sizeof tm);
    tm.tm_year = 2023 - 1900;
    tm.tm_mon = 0;
    tm.tm_mday = 32;            /* Jan 32 = Feb 1 */
    tm.tm_hour = 25;            /* +1 day, 01:00 */
    tm.tm_min = -30;            /* 00:30 */
    time_t t = mktime(&tm);
    CHECK_INT(t, 1675297800LL); /* 2023-02-02 00:30:00 */
    CHECK(tm.tm_mon == 1 && tm.tm_mday == 2 && tm.tm_hour == 0 && tm.tm_min == 30);
    CHECK_INT(tm.tm_wday, 4);
    CHECK_INT(tm.tm_yday, 32);

    memset(&tm, 0, sizeof tm);
    tm.tm_year = 2024 - 1900;
    tm.tm_mon = 13;             /* Feb 2025 */
    tm.tm_mday = 0;             /* last day of Jan 2025 */
    t = mktime(&tm);
    CHECK_INT(t, 1738281600LL);
    CHECK(tm.tm_year == 125 && tm.tm_mon == 0 && tm.tm_mday == 31);

    memset(&tm, 0, sizeof tm);
    tm.tm_year = 2024 - 1900;
    tm.tm_mon = -1;             /* Dec 2023 */
    tm.tm_mday = 1;
    t = mktime(&tm);
    CHECK_INT(t, 1701388800LL);

    /* round trip over a wide range, one value every ~3.7 days */
    int bad = 0;
    for (long long s = -62135596800LL; s < 253402300799LL; s += 318211LL * 1009LL) {
        time_t tt = (time_t)s;
        struct tm a;
        if (!gmtime_r(&tt, &a) || mktime(&a) != tt) bad++;
    }
    CHECK_INT(bad, 0);

    /* far beyond the representable years: an error, not garbage */
    time_t huge = (time_t)9000000000000000000LL;
    errno = 0;
    CHECK(gmtime_r(&huge, &tm) == NULL);
    CHECK_INT(errno, EOVERFLOW);
    CHECK(gmtime(NULL) == NULL);
}

static void test_strftime(void) {
    char buf[128];
    for (size_t i = 0; i < sizeof strf_cases / sizeof strf_cases[0]; i++) {
        time_t t = (time_t)strf_cases[i].t;
        struct tm tm;
        gmtime_r(&t, &tm);
        size_t n = strftime(buf, sizeof buf, strf_cases[i].fmt, &tm);
        t_count++;
        if (n != strlen(strf_cases[i].want) || strcmp(buf, strf_cases[i].want) != 0) {
            t_fail++;
            printf("  FAIL strftime(%lld, \"%s\") = \"%s\", want \"%s\"\n", strf_cases[i].t,
                   strf_cases[i].fmt, buf, strf_cases[i].want);
        }
    }
    /* too small a buffer: 0, as ISO C requires */
    time_t t = 0;
    struct tm tm;
    gmtime_r(&t, &tm);
    CHECK_INT(strftime(buf, 5, "%Y-%m", &tm), 0);
    CHECK_INT(strftime(buf, 8, "%Y-%m", &tm), 7);
    CHECK_STR(asctime(&tm), "Thu Jan  1 00:00:00 1970\n");
    CHECK_STR(ctime(&t), "Thu Jan  1 00:00:00 1970\n");
}

static void test_clock(void) {
    time_t now = time(NULL);
    time_t now2;
    CHECK(now > 1700000000);
    CHECK(time(&now2) >= now && now2 >= now);
    CHECK(clock() >= 0);
    CHECK(difftime(10, 4) == 6.0);
    struct timespec ts;
    CHECK_INT(timespec_get(&ts, TIME_UTC), TIME_UTC);
    CHECK(ts.tv_sec >= now && ts.tv_nsec >= 0 && ts.tv_nsec < 1000000000L);
    CHECK_INT(timespec_get(&ts, 99), 0);

    /* a kernel whose clock is not set: "no calendar time", not 1969 */
    nxc_host_rtc_broken(1);
    time_t out = 5;
    CHECK_INT(time(&out), -1);
    CHECK_INT(out, -1);
    CHECK_INT(timespec_get(&ts, TIME_UTC), 0);
    nxc_host_rtc_broken(0);
    CHECK(time(NULL) > 1700000000);
}

int main(void) {
    test_gmtime();
    test_mktime_normalise();
    test_strftime();
    test_clock();
    return t_finish("libc_time");
}
