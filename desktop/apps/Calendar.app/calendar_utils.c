/*
 * NeolyxOS Calendar Utilities
 * 
 * Shared calendar/time utility functions used by desktop shell.
 * Extracted from Calendar.app to avoid linking render dependencies.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include "calendar.h"

/* Day and month name arrays - shared with desktop shell */
const char *day_names_short[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
const char *day_names_full[7] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
const char *month_names_short[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
const char *month_names_full[12] = {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"};

/* Check leap year */
bool calendar_is_leap_year(int year) {
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

/* Days in month */
int calendar_days_in_month(int year, int month) {
    static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12) return 30;
    int d = days[month - 1];
    if (month == 2 && calendar_is_leap_year(year)) d = 29;
    return d;
}

/* Day of week (Zeller's formula) - returns 0=Sunday, 1=Monday, ..., 6=Saturday */
int calendar_day_of_week(int year, int month, int day) {
    if (month < 3) { month += 12; year--; }
    int k = year % 100;
    int j = year / 100;
    int h = (day + (13*(month+1))/5 + k + k/4 + j/4 - 2*j) % 7;
    return ((h + 6) % 7);
}
