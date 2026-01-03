/*
 * NeolyxOS Calendar - Regional Festivals and Moon Phases
 * 
 * Region-aware calendar with festival themes and lunar cycle.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef CALENDAR_REGIONS_H
#define CALENDAR_REGIONS_H

#include <stdint.h>
#include <stdbool.h>

/* Supported regions */
typedef enum {
    REGION_INDIA = 0,
    REGION_USA,
    REGION_EUROPE,
    REGION_CHINA,
    REGION_JAPAN,
    REGION_MIDDLE_EAST,
    REGION_COUNT
} calendar_region_t;

/* Moon phases (8 phases in lunar cycle) */
typedef enum {
    MOON_NEW = 0,        /* Amavasya (no moon) */
    MOON_WAXING_CRESCENT,
    MOON_FIRST_QUARTER,
    MOON_WAXING_GIBBOUS,
    MOON_FULL,           /* Purnima (full moon) */
    MOON_WANING_GIBBOUS,
    MOON_LAST_QUARTER,
    MOON_WANING_CRESCENT,
    MOON_PHASE_COUNT
} moon_phase_t;

/* Festival types */
typedef enum {
    FESTIVAL_NONE = 0,
    /* Indian festivals */
    FESTIVAL_DIWALI,
    FESTIVAL_HOLI,
    FESTIVAL_PONGAL,
    FESTIVAL_DUSSEHRA,
    FESTIVAL_GANESH_CHATURTHI,
    FESTIVAL_RAKSHA_BANDHAN,
    FESTIVAL_ONAM,
    FESTIVAL_NAVRATRI,
    FESTIVAL_MAKAR_SANKRANTI,
    /* USA festivals */
    FESTIVAL_THANKSGIVING,
    FESTIVAL_INDEPENDENCE_DAY_USA,
    FESTIVAL_HALLOWEEN,
    FESTIVAL_MEMORIAL_DAY,
    /* European festivals */
    FESTIVAL_CHRISTMAS,
    FESTIVAL_EASTER,
    FESTIVAL_NEW_YEAR,
    /* Chinese festivals */
    FESTIVAL_CHINESE_NEW_YEAR,
    FESTIVAL_MID_AUTUMN,
    FESTIVAL_DRAGON_BOAT,
    /* Japanese festivals */
    FESTIVAL_CHERRY_BLOSSOM,
    FESTIVAL_OBON,
    FESTIVAL_SHICHI_GO_SAN,
    /* Universal */
    FESTIVAL_MOTHERS_DAY,
    FESTIVAL_FATHERS_DAY,
    FESTIVAL_VALENTINES,
    FESTIVAL_COUNT
} festival_type_t;

/* Festival data */
typedef struct {
    festival_type_t type;
    const char *name;
    const char *local_name;
    uint8_t month;
    uint8_t day;
    uint32_t theme_color;
    uint32_t icon_color;
    const char *emoji;
} festival_t;

/* Moon phase names */
typedef struct {
    moon_phase_t phase;
    const char *english_name;
    const char *hindi_name;  /* For India region */
    const char *symbol;
} moon_info_t;

/* Region configuration */
typedef struct {
    calendar_region_t region;
    const char *name;
    const char *locale;
    const char *timezone;
    int utc_offset_mins;
    bool use_lunar_calendar;
    const festival_t *festivals;
    int festival_count;
} region_config_t;

/* Functions */
calendar_region_t region_detect(void);
const region_config_t *region_get_config(calendar_region_t region);
void region_set(calendar_region_t region);

/* Moon phase functions */
moon_phase_t moon_get_phase(int year, int month, int day);
const moon_info_t *moon_get_info(moon_phase_t phase);
bool moon_is_purnima(int year, int month, int day);
bool moon_is_amavasya(int year, int month, int day);

/* Festival functions */
const festival_t *festival_get_today(calendar_region_t region, int month, int day);
const festival_t *festival_get_upcoming(calendar_region_t region, int month, int day, int count);
uint32_t festival_get_theme_color(festival_type_t type);
void festival_apply_theme(festival_type_t type);

/* Calendar effects based on region */
void calendar_apply_regional_effects(calendar_region_t region, int month, int day);
void calendar_render_moon_phase(uint32_t *fb, int x, int y, int size, moon_phase_t phase);
void calendar_render_festival_icon(uint32_t *fb, int x, int y, int size, festival_type_t festival);

#endif /* CALENDAR_REGIONS_H */
