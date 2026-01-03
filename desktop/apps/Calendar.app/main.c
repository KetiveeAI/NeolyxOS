/*
 * NeolyxOS Calendar Application - Main Entry Point
 * 
 * Full-featured calendar with themes, clock faces, and live icon.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include "calendar.h"
#include <string.h>

/* Day and month name arrays */
const char *day_names_short[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
const char *day_names_full[7] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
const char *month_names_short[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
const char *month_names_full[12] = {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"};

/* Theme color definitions - 7 themes */
static const theme_colors_t themes[THEME_COUNT] = {
    /* THEME_MIDNIGHT - Dark blue/purple (default) */
    {0xFF0A0B1A, 0xFFFFFFFF, 0xFF808090, 0xFF4080B0, 0xFF6090C0, 0xFF303050, "Midnight"},
    
    /* THEME_ARCTIC - White/light blue */
    {0xFFF0F5FF, 0xFF1A1A2E, 0xFF505060, 0xFF2080D0, 0xFF60A0E0, 0xFFD0D5E0, "Arctic"},
    
    /* THEME_SUNSET - Orange/red gradient */
    {0xFF1A0A0A, 0xFFFFFFFF, 0xFFD0A080, 0xFFFF6040, 0xFFFF8060, 0xFF503020, "Sunset"},
    
    /* THEME_FOREST - Green tones */
    {0xFF0A1A0A, 0xFFE0FFE0, 0xFF80A080, 0xFF40B060, 0xFF60D080, 0xFF204020, "Forest"},
    
    /* THEME_OCEAN - Teal/aqua */
    {0xFF0A1A1A, 0xFFE0FFFF, 0xFF60A0A0, 0xFF20B0B0, 0xFF40D0D0, 0xFF204040, "Ocean"},
    
    /* THEME_SAKURA - Pink/cherry blossom */
    {0xFF1A0A14, 0xFFFFE0F0, 0xFFA08090, 0xFFFF80B0, 0xFFFFA0C0, 0xFF402030, "Sakura"},
    
    /* THEME_MONOCHROME - Black and white */
    {0xFF101010, 0xFFFFFFFF, 0xFF808080, 0xFFD0D0D0, 0xFFE0E0E0, 0xFF404040, "Monochrome"},
};

/* Global calendar context */
static calendar_ctx_t g_calendar;

/* External functions */
extern void desktop_draw_text(int x, int y, const char *text, uint32_t color);
extern void desktop_fill_rect(int x, int y, int w, int h, uint32_t color);
extern void fill_circle(int cx, int cy, int r, uint32_t color);
extern void draw_circle(int cx, int cy, int r, uint32_t color);
extern void fill_rounded_rect(int x, int y, int w, int h, int r, uint32_t color);
extern uint64_t pit_get_ticks(void);

/* Get theme colors */
const theme_colors_t *calendar_get_theme(calendar_theme_t theme) {
    if (theme >= THEME_COUNT) theme = THEME_MIDNIGHT;
    return &themes[theme];
}

/* Set theme */
void calendar_set_theme(calendar_ctx_t *ctx, calendar_theme_t theme) {
    if (theme >= THEME_COUNT) theme = THEME_MIDNIGHT;
    ctx->current_theme = theme;
}

/* Cycle to next theme */
void calendar_next_theme(calendar_ctx_t *ctx) {
    ctx->current_theme = (ctx->current_theme + 1) % THEME_COUNT;
}

/* Set clock face */
void calendar_set_clock_face(calendar_ctx_t *ctx, clock_face_t face) {
    if (face >= CLOCK_FACE_COUNT) face = CLOCK_FACE_DIGITAL;
    ctx->current_face = face;
}

/* Cycle to next clock face */
void calendar_next_clock_face(calendar_ctx_t *ctx) {
    ctx->current_face = (ctx->current_face + 1) % CLOCK_FACE_COUNT;
}

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

/* Day of week (Zeller's formula) */
int calendar_day_of_week(int year, int month, int day) {
    if (month < 3) { month += 12; year--; }
    int k = year % 100;
    int j = year / 100;
    int h = (day + (13*(month+1))/5 + k + k/4 + j/4 - 2*j) % 7;
    return ((h + 6) % 7);  /* 0=Sunday */
}

/* Initialize calendar */
void calendar_init(calendar_ctx_t *ctx) {
    memset(ctx, 0, sizeof(calendar_ctx_t));
    
    /* Default values */
    ctx->current_time.year = 2025;
    ctx->current_time.month = 12;
    ctx->current_time.day = 26;
    ctx->current_time.weekday = 4;  /* Thursday */
    ctx->current_time.hour = 7;
    ctx->current_time.minute = 49;
    ctx->current_time.second = 0;
    
    ctx->view_month = 12;
    ctx->view_year = 2025;
    ctx->current_theme = THEME_MIDNIGHT;
    ctx->current_face = CLOCK_FACE_DIGITAL;
    ctx->show_seconds = true;
    ctx->use_24h = true;
    ctx->running = true;
    
    /* Regional settings - default to India */
    ctx->current_region = 0;  /* REGION_INDIA */
    ctx->show_moon_phase = true;
    ctx->show_festivals = true;
    ctx->current_moon_phase = 4;  /* Near full moon */
    ctx->active_festival = 14;  /* FESTIVAL_CHRISTMAS */
}

/* Update time from system ticks */
void calendar_update_time(calendar_ctx_t *ctx) {
    uint64_t ticks = pit_get_ticks();
    uint32_t secs = (uint32_t)(ticks / 1000);
    
    /* Base time: 07:37 IST + elapsed seconds */
    uint32_t base_secs = 7 * 3600 + 37 * 60;
    uint32_t total_secs = base_secs + secs;
    
    ctx->current_time.second = total_secs % 60;
    ctx->current_time.minute = (total_secs / 60) % 60;
    ctx->current_time.hour = (total_secs / 3600) % 24;
}

/* Render live icon showing actual time */
void calendar_render_live_icon(calendar_ctx_t *ctx, uint32_t *fb, int x, int y, int size) {
    (void)fb;
    const theme_colors_t *theme = calendar_get_theme(ctx->current_theme);
    
    /* Clock face background */
    fill_circle(x + size/2, y + size/2, size/2, theme->background);
    draw_circle(x + size/2, y + size/2, size/2 - 1, theme->border);
    
    int cx = x + size/2;
    int cy = y + size/2;
    int r = size/2 - 4;
    
    /* Hour hand */
    float hour_angle = (ctx->current_time.hour % 12 + ctx->current_time.minute / 60.0f) * 30.0f - 90.0f;
    int hx = cx + (int)(r * 0.5f * (hour_angle < -45 || hour_angle > 45 ? 0.7f : 1.0f));
    int hy = cy + (int)(r * 0.5f * (hour_angle >= -45 && hour_angle <= 45 ? 0.7f : 1.0f));
    /* Simplified: draw a small filled circle for hour hand tip */
    fill_circle(hx, hy, 2, theme->text_primary);
    
    /* Minute hand */
    int min_y_offset = (ctx->current_time.minute < 30) ? -r * 2/3 : r * 2/3;
    int min_x_offset = (ctx->current_time.minute > 15 && ctx->current_time.minute < 45) ? 
                       ((ctx->current_time.minute < 30) ? r/2 : -r/2) : 0;
    fill_circle(cx + min_x_offset/2, cy + min_y_offset/2, 1, theme->accent);
    
    /* Center dot */
    fill_circle(cx, cy, 2, theme->accent);
    
    /* Small time text below (for digital overlay) */
    char time_str[8];
    time_str[0] = '0' + (ctx->current_time.hour / 10);
    time_str[1] = '0' + (ctx->current_time.hour % 10);
    time_str[2] = ':';
    time_str[3] = '0' + (ctx->current_time.minute / 10);
    time_str[4] = '0' + (ctx->current_time.minute % 10);
    time_str[5] = '\0';
    desktop_draw_text(x + size/2 - 15, y + size - 8, time_str, theme->text_secondary);
}

/* Render clock widget based on current face */
void calendar_render_clock_widget(calendar_ctx_t *ctx, uint32_t *fb, int x, int y, int size) {
    const theme_colors_t *theme = calendar_get_theme(ctx->current_theme);
    (void)fb;
    
    /* Background */
    fill_rounded_rect(x, y, size, size, 12, theme->background);
    
    switch (ctx->current_face) {
        case CLOCK_FACE_DIGITAL: {
            /* Large digital time */
            char time_str[16];
            if (ctx->show_seconds) {
                time_str[0] = '0' + (ctx->current_time.hour / 10);
                time_str[1] = '0' + (ctx->current_time.hour % 10);
                time_str[2] = ':';
                time_str[3] = '0' + (ctx->current_time.minute / 10);
                time_str[4] = '0' + (ctx->current_time.minute % 10);
                time_str[5] = ':';
                time_str[6] = '0' + (ctx->current_time.second / 10);
                time_str[7] = '0' + (ctx->current_time.second % 10);
                time_str[8] = '\0';
            } else {
                time_str[0] = '0' + (ctx->current_time.hour / 10);
                time_str[1] = '0' + (ctx->current_time.hour % 10);
                time_str[2] = ':';
                time_str[3] = '0' + (ctx->current_time.minute / 10);
                time_str[4] = '0' + (ctx->current_time.minute % 10);
                time_str[5] = '\0';
            }
            desktop_draw_text(x + size/4, y + size/2 - 5, time_str, theme->text_primary);
            break;
        }
        
        case CLOCK_FACE_ANALOG:
        case CLOCK_FACE_MINIMAL:
        case CLOCK_FACE_ROMAN: {
            /* Analog clock face */
            int cx = x + size/2;
            int cy = y + size/2;
            int r = size/2 - 10;
            
            draw_circle(cx, cy, r, theme->border);
            
            /* Hour markers */
            if (ctx->current_face != CLOCK_FACE_MINIMAL) {
                for (int h = 0; h < 12; h++) {
                    int hx = cx + (int)(r * 0.85f);  /* Simplified positions */
                    int hy = cy;
                    fill_circle(hx, hy, 2, theme->text_secondary);
                }
            }
            
            /* Hands - simplified */
            fill_circle(cx, cy, 3, theme->accent);
            break;
        }
        
        case CLOCK_FACE_BINARY: {
            /* Binary clock - 6 columns for HH:MM:SS */
            int col_w = size / 8;
            for (int c = 0; c < 6; c++) {
                uint8_t val;
                if (c == 0) val = ctx->current_time.hour / 10;
                else if (c == 1) val = ctx->current_time.hour % 10;
                else if (c == 2) val = ctx->current_time.minute / 10;
                else if (c == 3) val = ctx->current_time.minute % 10;
                else if (c == 4) val = ctx->current_time.second / 10;
                else val = ctx->current_time.second % 10;
                
                for (int b = 0; b < 4; b++) {
                    int bx = x + 10 + c * col_w;
                    int by = y + 10 + (3-b) * (size / 5);
                    uint32_t col = (val & (1 << b)) ? theme->accent : theme->border;
                    fill_circle(bx + col_w/2, by + 8, 6, col);
                }
            }
            break;
        }
        
        case CLOCK_FACE_FLIP: {
            /* Flip clock style - two panels */
            int panel_w = size / 2 - 10;
            int panel_h = size / 2;
            
            /* Hours panel */
            fill_rounded_rect(x + 5, y + size/4, panel_w, panel_h, 4, theme->border);
            char hrs[3] = {'0' + (ctx->current_time.hour / 10), '0' + (ctx->current_time.hour % 10), 0};
            desktop_draw_text(x + panel_w/2 - 5, y + size/4 + panel_h/2 - 5, hrs, theme->text_primary);
            
            /* Minutes panel */
            fill_rounded_rect(x + size/2 + 5, y + size/4, panel_w, panel_h, 4, theme->border);
            char mins[3] = {'0' + (ctx->current_time.minute / 10), '0' + (ctx->current_time.minute % 10), 0};
            desktop_draw_text(x + size/2 + panel_w/2, y + size/4 + panel_h/2 - 5, mins, theme->text_primary);
            break;
        }
        
        default:
            break;
    }
}

/* Render month calendar view */
void calendar_render_month_view(calendar_ctx_t *ctx, uint32_t *fb, int x, int y, int w, int h) {
    const theme_colors_t *theme = calendar_get_theme(ctx->current_theme);
    (void)fb;
    
    /* Background */
    fill_rounded_rect(x, y, w, h, 8, theme->background);
    
    /* Month/Year header */
    char header[32];
    const char *mon = month_names_full[ctx->view_month - 1];
    int idx = 0;
    while (*mon && idx < 20) header[idx++] = *mon++;
    header[idx++] = ' ';
    header[idx++] = '0' + (ctx->view_year / 1000);
    header[idx++] = '0' + ((ctx->view_year / 100) % 10);
    header[idx++] = '0' + ((ctx->view_year / 10) % 10);
    header[idx++] = '0' + (ctx->view_year % 10);
    header[idx] = '\0';
    
    desktop_draw_text(x + w/2 - 50, y + 10, header, theme->text_primary);
    
    /* Day headers */
    int cell_w = w / 7;
    for (int d = 0; d < 7; d++) {
        desktop_draw_text(x + d * cell_w + cell_w/2 - 8, y + 30, day_names_short[d], theme->text_secondary);
    }
    
    /* Calendar grid */
    int first_day = calendar_day_of_week(ctx->view_year, ctx->view_month, 1);
    int days = calendar_days_in_month(ctx->view_year, ctx->view_month);
    int row = 0, col = first_day;
    
    for (int day = 1; day <= days; day++) {
        int dx = x + col * cell_w + cell_w/2 - 5;
        int dy = y + 50 + row * 18;
        
        char day_str[3];
        day_str[0] = (day >= 10) ? ('0' + day/10) : ' ';
        day_str[1] = '0' + (day % 10);
        day_str[2] = '\0';
        
        /* Highlight current day */
        uint32_t col_txt = theme->text_primary;
        if (day == ctx->current_time.day && ctx->view_month == ctx->current_time.month) {
            fill_circle(dx + 5, dy + 4, 10, theme->accent);
            col_txt = theme->background;
        }
        
        desktop_draw_text(dx, dy, day_str, col_txt);
        
        col++;
        if (col >= 7) { col = 0; row++; }
    }
}

/* Main entry point */
int main(void) {
    calendar_init(&g_calendar);
    
    while (g_calendar.running) {
        calendar_update_time(&g_calendar);
        
        /* Main render loop would be here in windowed context */
        for (volatile int i = 0; i < 100000; i++);
    }
    
    return 0;
}
