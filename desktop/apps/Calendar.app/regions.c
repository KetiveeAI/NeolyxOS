/*
 * NeolyxOS Calendar - Regional Festivals and Moon Phases Implementation
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include "regions.h"
#include "calendar.h"
#include <string.h>

/* Moon phase names */
static const moon_info_t moon_phases[MOON_PHASE_COUNT] = {
    {MOON_NEW,              "New Moon",         "Amavasya",    "🌑"},
    {MOON_WAXING_CRESCENT,  "Waxing Crescent",  "Shukla 2-7",  "🌒"},
    {MOON_FIRST_QUARTER,    "First Quarter",    "Shukla 8",    "🌓"},
    {MOON_WAXING_GIBBOUS,   "Waxing Gibbous",   "Shukla 9-14", "🌔"},
    {MOON_FULL,             "Full Moon",        "Purnima",     "🌕"},
    {MOON_WANING_GIBBOUS,   "Waning Gibbous",   "Krishna 2-7", "🌖"},
    {MOON_LAST_QUARTER,     "Last Quarter",     "Krishna 8",   "🌗"},
    {MOON_WANING_CRESCENT,  "Waning Crescent",  "Krishna 9-14","🌘"},
};

/* Indian festivals */
static const festival_t indian_festivals[] = {
    {FESTIVAL_DIWALI,           "Diwali",           "दीवाली",        10, 24, 0xFFFF8C00, 0xFFFFD700, "🪔"},
    {FESTIVAL_HOLI,             "Holi",             "होली",          3,  25, 0xFFFF1493, 0xFF00FF00, "🎨"},
    {FESTIVAL_PONGAL,           "Pongal",           "पोंगल",         1,  14, 0xFFFFA500, 0xFFFFD700, "🍚"},
    {FESTIVAL_DUSSEHRA,         "Dussehra",         "दशहरा",         10, 12, 0xFFFF4500, 0xFFFFD700, "🏹"},
    {FESTIVAL_GANESH_CHATURTHI, "Ganesh Chaturthi", "गणेश चतुर्थी",   9,  7,  0xFFFF6347, 0xFFFFD700, "🐘"},
    {FESTIVAL_RAKSHA_BANDHAN,   "Raksha Bandhan",   "रक्षा बंधन",    8,  19, 0xFFFF69B4, 0xFFFFD700, "🎀"},
    {FESTIVAL_ONAM,             "Onam",             "ओणम",          8,  29, 0xFF00CED1, 0xFFFFD700, "🌸"},
    {FESTIVAL_NAVRATRI,         "Navratri",         "नवरात्रि",       10, 3,  0xFFFF1493, 0xFFFFD700, "💃"},
    {FESTIVAL_MAKAR_SANKRANTI,  "Makar Sankranti",  "मकर संक्रांति",  1,  14, 0xFFFFA500, 0xFFFFEB3B, "🌾"},
    {FESTIVAL_CHRISTMAS,        "Christmas",        "क्रिसमस",       12, 25, 0xFF228B22, 0xFFFF0000, "🎄"},
    {FESTIVAL_NEW_YEAR,         "New Year",         "नववर्ष",        1,  1,  0xFFFFD700, 0xFFFFFFFF, "🎉"},
};

/* USA festivals */
static const festival_t usa_festivals[] = {
    {FESTIVAL_THANKSGIVING,       "Thanksgiving",      "Thanksgiving",     11, 28, 0xFFFFA500, 0xFF8B4513, "🦃"},
    {FESTIVAL_INDEPENDENCE_DAY_USA, "Independence Day", "July 4th",        7,  4,  0xFF0000FF, 0xFFFF0000, "🎆"},
    {FESTIVAL_HALLOWEEN,          "Halloween",         "Halloween",        10, 31, 0xFFFF6600, 0xFF800080, "🎃"},
    {FESTIVAL_MEMORIAL_DAY,       "Memorial Day",      "Memorial Day",     5,  27, 0xFF0000FF, 0xFFFFFFFF, "🇺🇸"},
    {FESTIVAL_CHRISTMAS,          "Christmas",         "Christmas",        12, 25, 0xFF228B22, 0xFFFF0000, "🎄"},
    {FESTIVAL_NEW_YEAR,           "New Year",          "New Year",         1,  1,  0xFFFFD700, 0xFFFFFFFF, "🎉"},
    {FESTIVAL_VALENTINES,         "Valentine's Day",   "Valentine's",      2,  14, 0xFFFF1493, 0xFFFF0000, "❤️"},
    {FESTIVAL_EASTER,             "Easter",            "Easter",           4,  20, 0xFFFFB6C1, 0xFFFFA500, "🐰"},
    {FESTIVAL_MOTHERS_DAY,        "Mother's Day",      "Mother's Day",     5,  12, 0xFFFF69B4, 0xFFFFB6C1, "💐"},
    {FESTIVAL_FATHERS_DAY,        "Father's Day",      "Father's Day",     6,  16, 0xFF4169E1, 0xFF1E90FF, "👔"},
};

/* European festivals */
static const festival_t europe_festivals[] = {
    {FESTIVAL_CHRISTMAS,   "Christmas",   "Weihnachten",  12, 25, 0xFF228B22, 0xFFFF0000, "🎄"},
    {FESTIVAL_EASTER,      "Easter",      "Ostern",       4,  20, 0xFFFFB6C1, 0xFFFFA500, "🐰"},
    {FESTIVAL_NEW_YEAR,    "New Year",    "Neujahr",      1,  1,  0xFFFFD700, 0xFFFFFFFF, "🎉"},
    {FESTIVAL_VALENTINES,  "Valentine's", "Valentinstag", 2,  14, 0xFFFF1493, 0xFFFF0000, "❤️"},
};

/* Chinese festivals */
static const festival_t china_festivals[] = {
    {FESTIVAL_CHINESE_NEW_YEAR, "Chinese New Year", "春节",   1,  29, 0xFFFF0000, 0xFFFFD700, "🧧"},
    {FESTIVAL_MID_AUTUMN,       "Mid-Autumn",       "中秋节", 9,  17, 0xFFFFD700, 0xFFFF8C00, "🥮"},
    {FESTIVAL_DRAGON_BOAT,      "Dragon Boat",      "端午节", 6,  10, 0xFF228B22, 0xFFFFD700, "🐉"},
    {FESTIVAL_CHRISTMAS,        "Christmas",        "圣诞节", 12, 25, 0xFF228B22, 0xFFFF0000, "🎄"},
    {FESTIVAL_NEW_YEAR,         "New Year",         "元旦",   1,  1,  0xFFFFD700, 0xFFFFFFFF, "🎉"},
};

/* Japanese festivals */
static const festival_t japan_festivals[] = {
    {FESTIVAL_CHERRY_BLOSSOM, "Cherry Blossom", "桜祭り",    4,  1,  0xFFFFB6C1, 0xFFFF69B4, "🌸"},
    {FESTIVAL_OBON,           "Obon",           "お盆",      8,  15, 0xFFFFD700, 0xFFFF8C00, "🏮"},
    {FESTIVAL_SHICHI_GO_SAN,  "Shichi-Go-San",  "七五三",    11, 15, 0xFFFF69B4, 0xFFFFD700, "👘"},
    {FESTIVAL_CHRISTMAS,      "Christmas",      "クリスマス", 12, 25, 0xFF228B22, 0xFFFF0000, "🎄"},
    {FESTIVAL_NEW_YEAR,       "New Year",       "正月",      1,  1,  0xFFFFD700, 0xFFFFFFFF, "🎍"},
};

/* Region configurations */
static const region_config_t regions[REGION_COUNT] = {
    {REGION_INDIA,       "India",        "hi_IN", "IST",  330,  true,  indian_festivals, 11},
    {REGION_USA,         "USA",          "en_US", "EST",  -300, false, usa_festivals,    10},
    {REGION_EUROPE,      "Europe",       "en_EU", "CET",  60,   false, europe_festivals, 4},
    {REGION_CHINA,       "China",        "zh_CN", "CST",  480,  true,  china_festivals,  5},
    {REGION_JAPAN,       "Japan",        "ja_JP", "JST",  540,  false, japan_festivals,  5},
    {REGION_MIDDLE_EAST, "Middle East",  "ar_SA", "AST",  180,  true,  NULL,             0},
};

/* Current region (default: India) */
static calendar_region_t g_current_region = REGION_INDIA;

/* Get region config */
const region_config_t *region_get_config(calendar_region_t region) {
    if (region >= REGION_COUNT) region = REGION_INDIA;
    return &regions[region];
}

/* Set region */
void region_set(calendar_region_t region) {
    if (region < REGION_COUNT) {
        g_current_region = region;
    }
}

/* Detect region (placeholder - would use IP geolocation or user setting) */
calendar_region_t region_detect(void) {
    return g_current_region;
}

/* Calculate moon phase using simplified algorithm */
moon_phase_t moon_get_phase(int year, int month, int day) {
    /* Simplified moon phase calculation */
    /* Based on synodic month = 29.53 days */
    
    /* Reference: Jan 6, 2000 was a New Moon */
    int ref_year = 2000, ref_month = 1, ref_day = 6;
    
    /* Days since reference */
    int days = (year - ref_year) * 365 + (month - ref_month) * 30 + (day - ref_day);
    days += (year - ref_year) / 4;  /* Leap years approx */
    
    /* Moon cycle = 29.53 days, divide into 8 phases */
    int phase_day = days % 30;
    int phase = (phase_day * 8) / 30;
    
    if (phase >= MOON_PHASE_COUNT) phase = MOON_NEW;
    return (moon_phase_t)phase;
}

/* Get moon phase info */
const moon_info_t *moon_get_info(moon_phase_t phase) {
    if (phase >= MOON_PHASE_COUNT) phase = MOON_NEW;
    return &moon_phases[phase];
}

/* Check if Purnima (full moon) */
bool moon_is_purnima(int year, int month, int day) {
    return moon_get_phase(year, month, day) == MOON_FULL;
}

/* Check if Amavasya (new moon) */
bool moon_is_amavasya(int year, int month, int day) {
    return moon_get_phase(year, month, day) == MOON_NEW;
}

/* Get festival for today */
const festival_t *festival_get_today(calendar_region_t region, int month, int day) {
    const region_config_t *cfg = region_get_config(region);
    if (!cfg || !cfg->festivals) return NULL;
    
    for (int i = 0; i < cfg->festival_count; i++) {
        if (cfg->festivals[i].month == month && cfg->festivals[i].day == day) {
            return &cfg->festivals[i];
        }
    }
    return NULL;
}

/* Get festival theme color */
uint32_t festival_get_theme_color(festival_type_t type) {
    switch (type) {
        case FESTIVAL_DIWALI:             return 0xFFFF8C00;  /* Orange */
        case FESTIVAL_HOLI:               return 0xFFFF1493;  /* Pink */
        case FESTIVAL_CHRISTMAS:          return 0xFF228B22;  /* Green */
        case FESTIVAL_HALLOWEEN:          return 0xFFFF6600;  /* Orange */
        case FESTIVAL_CHINESE_NEW_YEAR:   return 0xFFFF0000;  /* Red */
        case FESTIVAL_CHERRY_BLOSSOM:     return 0xFFFFB6C1;  /* Light pink */
        default:                          return 0xFF4080B0;  /* Default blue */
    }
}

/* External drawing functions */
extern void fill_circle(int cx, int cy, int r, uint32_t color);
extern void draw_circle(int cx, int cy, int r, uint32_t color);
extern void fill_rounded_rect(int x, int y, int w, int h, int r, uint32_t color);

/* Render moon phase icon */
void calendar_render_moon_phase(uint32_t *fb, int x, int y, int size, moon_phase_t phase) {
    (void)fb;
    int cx = x + size/2;
    int cy = y + size/2;
    int r = size/2 - 2;
    
    uint32_t moon_color = 0xFFFFFFCC;  /* Pale yellow */
    uint32_t dark_color = 0xFF1A1A2E;  /* Dark background */
    
    switch (phase) {
        case MOON_NEW:
            draw_circle(cx, cy, r, 0xFF404040);  /* Dark outline */
            break;
        case MOON_FULL:
            fill_circle(cx, cy, r, moon_color);
            break;
        case MOON_FIRST_QUARTER:
            fill_circle(cx, cy, r, dark_color);
            fill_circle(cx + r/4, cy, r, moon_color);  /* Right half lit */
            break;
        case MOON_LAST_QUARTER:
            fill_circle(cx, cy, r, dark_color);
            fill_circle(cx - r/4, cy, r, moon_color);  /* Left half lit */
            break;
        case MOON_WAXING_CRESCENT:
            fill_circle(cx, cy, r, dark_color);
            fill_circle(cx + r/2, cy, r*3/4, moon_color);
            break;
        case MOON_WANING_CRESCENT:
            fill_circle(cx, cy, r, dark_color);
            fill_circle(cx - r/2, cy, r*3/4, moon_color);
            break;
        case MOON_WAXING_GIBBOUS:
            fill_circle(cx, cy, r, moon_color);
            fill_circle(cx - r/2, cy, r*3/4, dark_color);
            break;
        case MOON_WANING_GIBBOUS:
            fill_circle(cx, cy, r, moon_color);
            fill_circle(cx + r/2, cy, r*3/4, dark_color);
            break;
        default:
            draw_circle(cx, cy, r, moon_color);
            break;
    }
}

/* Render festival icon */
void calendar_render_festival_icon(uint32_t *fb, int x, int y, int size, festival_type_t festival) {
    (void)fb;
    int cx = x + size/2;
    int cy = y + size/2;
    
    switch (festival) {
        case FESTIVAL_DIWALI:
            /* Diya (lamp) shape */
            fill_circle(cx, cy + 4, size/4, 0xFFFFD700);
            fill_circle(cx, cy - 4, size/6, 0xFFFF6600);  /* Flame */
            break;
        case FESTIVAL_HOLI:
            /* Colorful circles */
            fill_circle(cx - 4, cy, size/5, 0xFFFF1493);
            fill_circle(cx + 4, cy, size/5, 0xFF00FF00);
            fill_circle(cx, cy - 4, size/5, 0xFFFFFF00);
            break;
        case FESTIVAL_CHRISTMAS:
            /* Tree shape */
            fill_circle(cx, cy - 4, size/3, 0xFF228B22);
            fill_rounded_rect(cx - 2, cy + size/4, 4, 6, 1, 0xFF8B4513);
            break;
        case FESTIVAL_HALLOWEEN:
            /* Pumpkin */
            fill_circle(cx, cy, size/3, 0xFFFF6600);
            break;
        default:
            fill_circle(cx, cy, size/4, 0xFF4080B0);
            break;
    }
}

/* Apply regional effects to calendar */
void calendar_apply_regional_effects(calendar_region_t region, int month, int day) {
    const festival_t *fest = festival_get_today(region, month, day);
    if (fest) {
        festival_apply_theme(fest->type);
    }
}

/* Apply festival theme */
void festival_apply_theme(festival_type_t type) {
    (void)type;
    /* Would modify global theme colors based on festival */
}
