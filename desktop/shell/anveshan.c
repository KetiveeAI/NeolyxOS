/*
 * NeolyxOS Anveshan Global Search Implementation
 * 
 * Unified search across apps, settings, and files.
 * Dynamic layout from settings, NXI icon integration.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include "../include/anveshan.h"
#include "../include/nxconfig.h"
#include <stddef.h>

/* ============ Static State ============ */

static anveshan_state_t g_anveshan;
static int g_initialized = 0;

/* App index */
#define MAX_INDEXED_APPS 64
static anveshan_app_t g_app_index[MAX_INDEXED_APPS];
static int g_app_count = 0;

/* Settings index (from Settings.app patterns) */
typedef struct {
    const char *keyword;
    const char *title;
    const char *description;
    int panel_id;
    const char *sub_page;
} setting_entry_t;

static const setting_entry_t g_settings_index[] = {
    { "wifi",       "Wi-Fi",            "Wireless network",     0, "wifi" },
    { "bluetooth",  "Bluetooth",        "Bluetooth devices",    1, NULL },
    { "theme",      "Theme",            "Light/Dark mode",      2, NULL },
    { "accent",     "Accent Color",     "System accent color",  2, "accent" },
    { "dock",       "Dock",             "Dock settings",        2, "dock" },
    { "wallpaper",  "Wallpaper",        "Desktop wallpaper",    2, "wallpaper" },
    { "display",    "Display",          "Screen settings",      3, NULL },
    { "brightness", "Brightness",       "Display brightness",   3, NULL },
    { "sound",      "Sound",            "Audio settings",       4, NULL },
    { "volume",     "Volume",           "System volume",        4, NULL },
    { "battery",    "Battery",          "Power settings",       5, NULL },
    { "password",   "Password",         "Account password",     6, "password" },
    { "privacy",    "Privacy",          "Privacy settings",     7, NULL },
    { "update",     "Software Update",  "System updates",       8, NULL },
    { "about",      "About This Mac",   "System info",          9, NULL },
};

#define SETTINGS_INDEX_SIZE (sizeof(g_settings_index) / sizeof(g_settings_index[0]))

/* ============ String Helpers ============ */

static int av_strlen(const char *s) {
    int len = 0;
    while (s && s[len]) len++;
    return len;
}

static void av_strcpy(char *dst, const char *src, int max) {
    int i = 0;
    while (src && src[i] && i < max - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static char av_tolower(char c) {
    return (c >= 'A' && c <= 'Z') ? c + 32 : c;
}

static int av_contains(const char *haystack, const char *needle) {
    if (!haystack || !needle || !needle[0]) return 0;
    
    int nlen = av_strlen(needle);
    
    for (int i = 0; haystack[i]; i++) {
        int match = 1;
        for (int j = 0; j < nlen && haystack[i + j]; j++) {
            if (av_tolower(haystack[i + j]) != av_tolower(needle[j])) {
                match = 0;
                break;
            }
        }
        if (match) return 1;
    }
    return 0;
}

/* ============ Drawing Primitives ============ */

static void av_put_pixel_alpha(uint32_t *fb, uint32_t pitch,
                                int32_t x, int32_t y, uint32_t color,
                                uint32_t screen_w, uint32_t screen_h) {
    if (x < 0 || x >= (int32_t)screen_w || y < 0 || y >= (int32_t)screen_h) return;
    
    uint8_t a = (color >> 24) & 0xFF;
    if (a == 0xFF) {
        fb[y * (pitch / 4) + x] = color;
        return;
    }
    if (a == 0x00) return;
    
    uint32_t bg = fb[y * (pitch / 4) + x];
    uint8_t br = (bg >> 16) & 0xFF, bg_ = (bg >> 8) & 0xFF, bb = bg & 0xFF;
    uint8_t fr = (color >> 16) & 0xFF, fg = (color >> 8) & 0xFF, fbb = color & 0xFF;
    
    uint8_t r = (fr * a + br * (255 - a)) / 255;
    uint8_t g = (fg * a + bg_ * (255 - a)) / 255;
    uint8_t b = (fbb * a + bb * (255 - a)) / 255;
    
    fb[y * (pitch / 4) + x] = 0xFF000000 | (r << 16) | (g << 8) | b;
}

static void av_fill_rect_alpha(uint32_t *fb, uint32_t pitch,
                                int32_t x, int32_t y, uint32_t w, uint32_t h,
                                uint32_t color, uint32_t sw, uint32_t sh) {
    for (uint32_t j = 0; j < h; j++) {
        for (uint32_t i = 0; i < w; i++) {
            av_put_pixel_alpha(fb, pitch, x + i, y + j, color, sw, sh);
        }
    }
}

static void av_fill_rounded_rect(uint32_t *fb, uint32_t pitch,
                                  int32_t x, int32_t y, uint32_t w, uint32_t h,
                                  int32_t r, uint32_t color,
                                  uint32_t sw, uint32_t sh) {
    if (r > (int32_t)(h / 2)) r = h / 2;
    if (r > (int32_t)(w / 2)) r = w / 2;
    
    av_fill_rect_alpha(fb, pitch, x + r, y, w - 2*r, h, color, sw, sh);
    av_fill_rect_alpha(fb, pitch, x, y + r, r, h - 2*r, color, sw, sh);
    av_fill_rect_alpha(fb, pitch, x + w - r, y + r, r, h - 2*r, color, sw, sh);
    
    for (int32_t dy = 0; dy < r; dy++) {
        for (int32_t dx = 0; dx < r; dx++) {
            int32_t dist_sq = (r - dx - 1) * (r - dx - 1) + (r - dy - 1) * (r - dy - 1);
            if (dist_sq <= r * r) {
                av_put_pixel_alpha(fb, pitch, x + dx, y + dy, color, sw, sh);
                av_put_pixel_alpha(fb, pitch, x + w - 1 - dx, y + dy, color, sw, sh);
                av_put_pixel_alpha(fb, pitch, x + dx, y + h - 1 - dy, color, sw, sh);
                av_put_pixel_alpha(fb, pitch, x + w - 1 - dx, y + h - 1 - dy, color, sw, sh);
            }
        }
    }
}

static void av_fill_pill(uint32_t *fb, uint32_t pitch,
                          int32_t x, int32_t y, uint32_t w, uint32_t h,
                          uint32_t color, uint32_t sw, uint32_t sh) {
    int32_t r = h / 2;
    if (r > (int32_t)(w / 2)) r = w / 2;
    av_fill_rounded_rect(fb, pitch, x, y, w, h, r, color, sw, sh);
}

static void av_fill_circle(uint32_t *fb, uint32_t pitch,
                            int32_t cx, int32_t cy, int32_t r, uint32_t color,
                            uint32_t sw, uint32_t sh) {
    for (int32_t dy = -r; dy <= r; dy++) {
        for (int32_t dx = -r; dx <= r; dx++) {
            if (dx*dx + dy*dy <= r*r) {
                av_put_pixel_alpha(fb, pitch, cx + dx, cy + dy, color, sw, sh);
            }
        }
    }
}

static void av_draw_circle(uint32_t *fb, uint32_t pitch,
                            int32_t cx, int32_t cy, int32_t r, uint32_t color,
                            uint32_t sw, uint32_t sh) {
    int32_t x = 0, y = r;
    int32_t d = 3 - 2 * r;
    while (x <= y) {
        av_put_pixel_alpha(fb, pitch, cx + x, cy + y, color, sw, sh);
        av_put_pixel_alpha(fb, pitch, cx - x, cy + y, color, sw, sh);
        av_put_pixel_alpha(fb, pitch, cx + x, cy - y, color, sw, sh);
        av_put_pixel_alpha(fb, pitch, cx - x, cy - y, color, sw, sh);
        av_put_pixel_alpha(fb, pitch, cx + y, cy + x, color, sw, sh);
        av_put_pixel_alpha(fb, pitch, cx - y, cy + x, color, sw, sh);
        av_put_pixel_alpha(fb, pitch, cx + y, cy - x, color, sw, sh);
        av_put_pixel_alpha(fb, pitch, cx - y, cy - x, color, sw, sh);
        if (d < 0) d += 4 * x + 6;
        else { d += 4 * (x - y) + 10; y--; }
        x++;
    }
}

/* 8x8 font for text rendering */
static const uint8_t av_font_8x8[][8] = {
    {0,0,0,0,0,0,0,0}, /* space */
    {0x10,0x10,0x10,0x10,0x10,0x00,0x10,0x00}, /* ! */
    {0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},
    /* 0-9 */
    {0x3C,0x66,0x6E,0x7E,0x76,0x66,0x3C,0x00},
    {0x18,0x38,0x18,0x18,0x18,0x18,0x3C,0x00},
    {0x3C,0x66,0x06,0x1C,0x30,0x60,0x7E,0x00},
    {0x3C,0x66,0x06,0x1C,0x06,0x66,0x3C,0x00},
    {0x0C,0x1C,0x3C,0x6C,0x7E,0x0C,0x0C,0x00},
    {0x7E,0x60,0x7C,0x06,0x06,0x66,0x3C,0x00},
    {0x1C,0x30,0x60,0x7C,0x66,0x66,0x3C,0x00},
    {0x7E,0x06,0x0C,0x18,0x30,0x30,0x30,0x00},
    {0x3C,0x66,0x66,0x3C,0x66,0x66,0x3C,0x00},
    {0x3C,0x66,0x66,0x3E,0x06,0x0C,0x38,0x00},
    {0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},
    /* A-Z */
    {0x18,0x3C,0x66,0x66,0x7E,0x66,0x66,0x00},
    {0x7C,0x66,0x66,0x7C,0x66,0x66,0x7C,0x00},
    {0x3C,0x66,0x60,0x60,0x60,0x66,0x3C,0x00},
    {0x78,0x6C,0x66,0x66,0x66,0x6C,0x78,0x00},
    {0x7E,0x60,0x60,0x78,0x60,0x60,0x7E,0x00},
    {0x7E,0x60,0x60,0x78,0x60,0x60,0x60,0x00},
    {0x3C,0x66,0x60,0x6E,0x66,0x66,0x3E,0x00},
    {0x66,0x66,0x66,0x7E,0x66,0x66,0x66,0x00},
    {0x3C,0x18,0x18,0x18,0x18,0x18,0x3C,0x00},
    {0x1E,0x0C,0x0C,0x0C,0x6C,0x6C,0x38,0x00},
    {0x66,0x6C,0x78,0x70,0x78,0x6C,0x66,0x00},
    {0x60,0x60,0x60,0x60,0x60,0x60,0x7E,0x00},
    {0xC6,0xEE,0xFE,0xD6,0xC6,0xC6,0xC6,0x00},
    {0x66,0x76,0x7E,0x7E,0x6E,0x66,0x66,0x00},
    {0x3C,0x66,0x66,0x66,0x66,0x66,0x3C,0x00},
    {0x7C,0x66,0x66,0x7C,0x60,0x60,0x60,0x00},
    {0x3C,0x66,0x66,0x66,0x6A,0x6C,0x36,0x00},
    {0x7C,0x66,0x66,0x7C,0x6C,0x66,0x66,0x00},
    {0x3C,0x66,0x60,0x3C,0x06,0x66,0x3C,0x00},
    {0x7E,0x18,0x18,0x18,0x18,0x18,0x18,0x00},
    {0x66,0x66,0x66,0x66,0x66,0x66,0x3C,0x00},
    {0x66,0x66,0x66,0x66,0x66,0x3C,0x18,0x00},
    {0xC6,0xC6,0xC6,0xD6,0xFE,0xEE,0xC6,0x00},
    {0x66,0x66,0x3C,0x18,0x3C,0x66,0x66,0x00},
    {0x66,0x66,0x66,0x3C,0x18,0x18,0x18,0x00},
    {0x7E,0x06,0x0C,0x18,0x30,0x60,0x7E,0x00},
};

static void av_draw_text(uint32_t *fb, uint32_t pitch,
                         int32_t x, int32_t y, const char *text, uint32_t color,
                         uint32_t sw, uint32_t sh) {
    while (*text) {
        uint8_t ch = *text;
        int idx = -1;
        
        if (ch == ' ') idx = 0;
        else if (ch >= '0' && ch <= '9') idx = ch - '0' + 16;
        else if (ch >= 'A' && ch <= 'Z') idx = ch - 'A' + 33;
        else if (ch >= 'a' && ch <= 'z') idx = ch - 'a' + 33;
        else if (ch == '|') { x += 8; text++; continue; }
        
        if (idx >= 0 && idx < 59) {
            for (int row = 0; row < 8; row++) {
                uint8_t bits = av_font_8x8[idx][row];
                for (int col = 0; col < 8; col++) {
                    if (bits & (0x80 >> col)) {
                        av_put_pixel_alpha(fb, pitch, x + col, y + row, color, sw, sh);
                    }
                }
            }
        }
        x += 8;
        text++;
    }
}

/* ============ Initialization ============ */

void anveshan_init(uint32_t screen_w, uint32_t screen_h) {
    if (g_initialized) return;
    
    g_anveshan.screen_width = screen_w;
    g_anveshan.screen_height = screen_h;
    g_anveshan.query[0] = '\0';
    g_anveshan.cursor_pos = 0;
    g_anveshan.active = 0;
    g_anveshan.open = 0;
    g_anveshan.result_count = 0;
    g_anveshan.selected_idx = -1;
    
    /* Load default settings */
    anveshan_settings_t defaults = ANVESHAN_DEFAULTS;
    g_anveshan.settings = defaults;
    
    /* Compute initial geometry */
    g_anveshan.bar_x = (screen_w - g_anveshan.settings.width) / 2;
    g_anveshan.bar_y = g_anveshan.settings.y_position;
    
    /* Build app index */
    anveshan_index_apps();
    
    g_initialized = 1;
}

void anveshan_load_settings(void) {
    nxc_config_t *config = nxc_load(NXC_USER_DEFAULTS);
    if (!config) return;
    
    g_anveshan.settings.width = nxc_get_int(config, "search", "width", 320);
    g_anveshan.settings.height = nxc_get_int(config, "search", "height", 36);
    g_anveshan.settings.opacity = (uint8_t)nxc_get_int(config, "search", "opacity", 180);
    g_anveshan.settings.max_suggestions = nxc_get_int(config, "search", "max_suggestions", 5);
    g_anveshan.settings.search_apps = nxc_get_bool(config, "search", "search_apps", 1);
    g_anveshan.settings.search_settings = nxc_get_bool(config, "search", "search_settings", 1);
    g_anveshan.settings.search_files = nxc_get_bool(config, "search", "search_files", 0);
    g_anveshan.settings.voice_enabled = nxc_get_bool(config, "search", "voice_enabled", 1);
    
    /* Recalculate geometry */
    g_anveshan.bar_x = (g_anveshan.screen_width - g_anveshan.settings.width) / 2;
}

void anveshan_save_settings(void) {
    nxc_config_t *config = nxc_load(NXC_USER_DEFAULTS);
    if (!config) return;
    
    nxc_set_int(config, "search", "width", g_anveshan.settings.width);
    nxc_set_int(config, "search", "height", g_anveshan.settings.height);
    nxc_set_int(config, "search", "opacity", g_anveshan.settings.opacity);
    nxc_set_int(config, "search", "max_suggestions", g_anveshan.settings.max_suggestions);
    nxc_set_bool(config, "search", "search_apps", g_anveshan.settings.search_apps);
    nxc_set_bool(config, "search", "search_settings", g_anveshan.settings.search_settings);
    nxc_set_bool(config, "search", "search_files", g_anveshan.settings.search_files);
    nxc_set_bool(config, "search", "voice_enabled", g_anveshan.settings.voice_enabled);
    
    nxc_save(config);
}

/* ============ App Indexing ============ */

void anveshan_index_apps(void) {
    /* Hardcoded for now - would scan desktop/apps/ in real implementation */
    static const struct {
        const char *name;
        const char *bundle_id;
        uint32_t color;
        const char *keywords;
    } builtin_apps[] = {
        { "Path",         "com.neolyx.path",       0xFFF5C518, "files finder folder browser" },
        { "Terminal",     "com.neolyx.terminal",   0xFF42C5F5, "shell command bash console" },
        { "Settings",     "com.neolyx.settings",   0xFF888888, "preferences system config" },
        { "Calculator",   "com.neolyx.calc",       0xFFFF9500, "math calculate" },
        { "Calendar",     "com.neolyx.calendar",   0xFFFF4080, "date schedule event" },
        { "Clock",        "com.neolyx.clock",      0xFF007AFF, "time alarm timer" },
        { "Notes",        "com.neolyx.notes",      0xFFFFCC00, "text write note" },
        { "Photos",       "com.neolyx.photos",     0xFFFF2D55, "image picture gallery" },
        { "Music",        "com.neolyx.music",      0xFFFF3B30, "audio song player" },
        { "Mail",         "com.neolyx.mail",       0xFF007AFF, "email message" },
    };
    
    g_app_count = 0;
    for (int i = 0; i < 10 && g_app_count < MAX_INDEXED_APPS; i++) {
        anveshan_app_t *app = &g_app_index[g_app_count++];
        av_strcpy(app->name, builtin_apps[i].name, 64);
        av_strcpy(app->bundle_id, builtin_apps[i].bundle_id, 64);
        app->icon_color = builtin_apps[i].color;
        av_strcpy(app->keywords, builtin_apps[i].keywords, 256);
    }
}

int anveshan_get_app_count(void) {
    return g_app_count;
}

/* ============ Search ============ */

void anveshan_query(const char *query) {
    if (!query) {
        anveshan_clear();
        return;
    }
    
    av_strcpy(g_anveshan.query, query, ANVESHAN_MAX_QUERY);
    g_anveshan.result_count = 0;
    
    if (!query[0]) {
        g_anveshan.open = 0;
        return;
    }
    
    int max_results = g_anveshan.settings.max_suggestions;
    if (max_results > ANVESHAN_MAX_RESULTS) max_results = ANVESHAN_MAX_RESULTS;
    
    /* Search apps */
    if (g_anveshan.settings.search_apps) {
        for (int i = 0; i < g_app_count && g_anveshan.result_count < max_results; i++) {
            anveshan_app_t *app = &g_app_index[i];
            int score = 0;
            
            if (av_contains(app->name, query)) score += 100;
            if (av_contains(app->keywords, query)) score += 50;
            
            if (score > 0) {
                anveshan_result_t *res = &g_anveshan.results[g_anveshan.result_count++];
                res->type = ANVESHAN_TYPE_APP;
                av_strcpy(res->title, app->name, 64);
                av_strcpy(res->subtitle, "Application", 128);
                res->icon_color = app->icon_color;
                res->score = score;
                av_strcpy(res->action.app.bundle_id, app->bundle_id, 64);
            }
        }
    }
    
    /* Search settings */
    if (g_anveshan.settings.search_settings) {
        for (int i = 0; i < (int)SETTINGS_INDEX_SIZE && g_anveshan.result_count < max_results; i++) {
            const setting_entry_t *entry = &g_settings_index[i];
            int score = 0;
            
            if (av_contains(entry->keyword, query)) score += 100;
            if (av_contains(entry->title, query)) score += 50;
            if (av_contains(entry->description, query)) score += 25;
            
            if (score > 0) {
                anveshan_result_t *res = &g_anveshan.results[g_anveshan.result_count++];
                res->type = ANVESHAN_TYPE_SETTING;
                av_strcpy(res->title, entry->title, 64);
                av_strcpy(res->subtitle, entry->description, 128);
                res->icon_color = 0xFF888888;
                res->score = score;
                res->action.setting.panel_id = entry->panel_id;
                if (entry->sub_page) {
                    av_strcpy(res->action.setting.sub_page, entry->sub_page, 32);
                } else {
                    res->action.setting.sub_page[0] = '\0';
                }
            }
        }
    }
    
    /* Sort by score */
    for (int a = 0; a < g_anveshan.result_count - 1; a++) {
        for (int b = a + 1; b < g_anveshan.result_count; b++) {
            if (g_anveshan.results[b].score > g_anveshan.results[a].score) {
                anveshan_result_t temp = g_anveshan.results[a];
                g_anveshan.results[a] = g_anveshan.results[b];
                g_anveshan.results[b] = temp;
            }
        }
    }
    
    g_anveshan.open = (g_anveshan.result_count > 0);
    g_anveshan.selected_idx = -1;
}

void anveshan_clear(void) {
    g_anveshan.query[0] = '\0';
    g_anveshan.cursor_pos = 0;
    g_anveshan.result_count = 0;
    g_anveshan.open = 0;
    g_anveshan.selected_idx = -1;
}

/* ============ Rendering ============ */

void anveshan_render(uint32_t *fb, uint32_t pitch) {
    if (!g_initialized) return;
    
    anveshan_settings_t *s = &g_anveshan.settings;
    uint32_t sw = g_anveshan.screen_width;
    uint32_t sh = g_anveshan.screen_height;
    
    int bar_x = g_anveshan.bar_x;
    int bar_y = g_anveshan.bar_y;
    int bar_w = s->width;
    int bar_h = s->height;
    
    /* Glass background with configurable opacity */
    uint32_t bg_color = ((uint32_t)s->opacity << 24) | (s->bg_color & 0x00FFFFFF);
    av_fill_pill(fb, pitch, bar_x, bar_y, bar_w, bar_h, bg_color, sw, sh);
    
    /* Border */
    uint32_t border_color = 0x40FFFFFF;
    av_draw_circle(fb, pitch, bar_x + bar_h/2, bar_y + bar_h/2, bar_h/2 - 1, border_color, sw, sh);
    av_draw_circle(fb, pitch, bar_x + bar_w - bar_h/2 - 1, bar_y + bar_h/2, bar_h/2 - 1, border_color, sw, sh);
    
    /* Search icon (magnifying glass) */
    int icon_x = bar_x + 22;
    int icon_y = bar_y + bar_h / 2;
    av_draw_circle(fb, pitch, icon_x, icon_y, 7, s->icon_color | 0xFF000000, sw, sh);
    for (int i = 0; i < 4; i++) {
        av_put_pixel_alpha(fb, pitch, icon_x + 5 + i, icon_y + 5 + i, 
                           s->icon_color | 0xFF000000, sw, sh);
    }
    
    /* Text (query or placeholder) */
    int text_x = bar_x + 42;
    int text_y = bar_y + (bar_h - 8) / 2;
    
    if (g_anveshan.query[0]) {
        av_draw_text(fb, pitch, text_x, text_y, g_anveshan.query, 0xFFFFFFFF, sw, sh);
        
        /* Cursor if active */
        if (g_anveshan.active) {
            int cursor_x = text_x + g_anveshan.cursor_pos * 8;
            av_fill_rect_alpha(fb, pitch, cursor_x, text_y, 2, 8, 0xFFFFFFFF, sw, sh);
        }
    } else {
        av_draw_text(fb, pitch, text_x, text_y, s->placeholder, s->text_color | 0xFF000000, sw, sh);
    }
    
    /* Microphone icon (if enabled) */
    if (s->voice_enabled) {
        int mic_x = bar_x + bar_w - 28;
        int mic_y = bar_y + (bar_h - 14) / 2;
        av_fill_rounded_rect(fb, pitch, mic_x, mic_y, 8, 14, 3, s->icon_color | 0xFF000000, sw, sh);
        av_fill_circle(fb, pitch, mic_x + 4, mic_y, 4, s->icon_color | 0xFF000000, sw, sh);
    }
    
    /* Dropdown with results */
    if (g_anveshan.open && g_anveshan.result_count > 0) {
        int dropdown_y = bar_y + bar_h + 8;
        int item_h = 36;
        int dropdown_h = g_anveshan.result_count * item_h + 16;
        
        /* Dropdown background */
        av_fill_rounded_rect(fb, pitch, bar_x, dropdown_y, bar_w, dropdown_h, 12,
                             ((uint32_t)(s->opacity - 20) << 24) | 0x202030, sw, sh);
        
        /* Results */
        for (int i = 0; i < g_anveshan.result_count; i++) {
            anveshan_result_t *res = &g_anveshan.results[i];
            int item_y = dropdown_y + 8 + i * item_h;
            
            /* Highlight selected */
            if (i == g_anveshan.selected_idx) {
                av_fill_rounded_rect(fb, pitch, bar_x + 8, item_y, bar_w - 16, item_h - 4, 8,
                                     0x40FFFFFF, sw, sh);
            }
            
            /* Icon (colored circle) */
            av_fill_circle(fb, pitch, bar_x + 28, item_y + item_h/2 - 2, 12, res->icon_color, sw, sh);
            
            /* Type indicator */
            const char *type_char = "?";
            if (res->type == ANVESHAN_TYPE_APP) type_char = "A";
            else if (res->type == ANVESHAN_TYPE_SETTING) type_char = "S";
            else if (res->type == ANVESHAN_TYPE_FILE) type_char = "F";
            av_draw_text(fb, pitch, bar_x + 24, item_y + item_h/2 - 6, type_char, 0xFFFFFFFF, sw, sh);
            
            /* Title */
            av_draw_text(fb, pitch, bar_x + 52, item_y + 4, res->title, 0xFFFFFFFF, sw, sh);
            
            /* Subtitle */
            av_draw_text(fb, pitch, bar_x + 52, item_y + 16, res->subtitle, 0xFF888888, sw, sh);
        }
    }
}

/* ============ Input Handling ============ */

int anveshan_handle_mouse(int mouse_x, int mouse_y, uint8_t buttons, uint8_t prev_buttons) {
    if (!g_initialized) return 0;
    
    int bar_x = g_anveshan.bar_x;
    int bar_y = g_anveshan.bar_y;
    int bar_w = g_anveshan.settings.width;
    int bar_h = g_anveshan.settings.height;
    
    /* Check click on search bar */
    if ((buttons & 0x01) && !(prev_buttons & 0x01)) {
        if (mouse_x >= bar_x && mouse_x < bar_x + bar_w &&
            mouse_y >= bar_y && mouse_y < bar_y + bar_h) {
            g_anveshan.active = 1;
            return 1;
        }
        
        /* Check click on dropdown items */
        if (g_anveshan.open && g_anveshan.result_count > 0) {
            int dropdown_y = bar_y + bar_h + 8;
            int item_h = 36;
            
            if (mouse_x >= bar_x && mouse_x < bar_x + bar_w &&
                mouse_y >= dropdown_y) {
                int idx = (mouse_y - dropdown_y - 8) / item_h;
                if (idx >= 0 && idx < g_anveshan.result_count) {
                    /* Activate result */
                    anveshan_result_t *res = &g_anveshan.results[idx];
                    if (res->type == ANVESHAN_TYPE_APP) {
                        nxevent_publish_app(NX_EVENT_APP_LAUNCHED, 
                                           0, res->title, res->action.app.bundle_id);
                    }
                    anveshan_clear();
                    g_anveshan.active = 0;
                    return 1;
                }
            }
        }
        
        /* Click outside - deactivate */
        g_anveshan.active = 0;
        g_anveshan.open = 0;
    }
    
    /* Hover over dropdown items */
    if (g_anveshan.open && g_anveshan.result_count > 0) {
        int dropdown_y = bar_y + bar_h + 8;
        int item_h = 36;
        
        if (mouse_x >= bar_x && mouse_x < bar_x + bar_w &&
            mouse_y >= dropdown_y) {
            int idx = (mouse_y - dropdown_y - 8) / item_h;
            if (idx >= 0 && idx < g_anveshan.result_count) {
                g_anveshan.selected_idx = idx;
            }
        }
    }
    
    return 0;
}

int anveshan_handle_key(uint8_t scancode, int pressed, char ascii) {
    if (!g_initialized || !g_anveshan.active || !pressed) return 0;
    
    /* Escape - close */
    if (scancode == 0x01) {
        anveshan_clear();
        g_anveshan.active = 0;
        return 1;
    }
    
    /* Enter - activate selected result */
    if (scancode == 0x1C) {
        if (g_anveshan.selected_idx >= 0 && g_anveshan.selected_idx < g_anveshan.result_count) {
            anveshan_result_t *res = &g_anveshan.results[g_anveshan.selected_idx];
            if (res->type == ANVESHAN_TYPE_APP) {
                nxevent_publish_app(NX_EVENT_APP_LAUNCHED, 0, res->title, res->action.app.bundle_id);
            }
            anveshan_clear();
            g_anveshan.active = 0;
        }
        return 1;
    }
    
    /* Arrow up/down - navigate results */
    if (scancode == 0x48) {  /* Up */
        if (g_anveshan.selected_idx > 0) g_anveshan.selected_idx--;
        else if (g_anveshan.result_count > 0) g_anveshan.selected_idx = g_anveshan.result_count - 1;
        return 1;
    }
    if (scancode == 0x50) {  /* Down */
        if (g_anveshan.selected_idx < g_anveshan.result_count - 1) g_anveshan.selected_idx++;
        else g_anveshan.selected_idx = 0;
        return 1;
    }
    
    /* Backspace */
    if (scancode == 0x0E) {
        int len = av_strlen(g_anveshan.query);
        if (len > 0) {
            g_anveshan.query[len - 1] = '\0';
            g_anveshan.cursor_pos = len - 1;
            anveshan_query(g_anveshan.query);
        }
        return 1;
    }
    
    /* Printable characters */
    if (ascii >= 32 && ascii < 127) {
        int len = av_strlen(g_anveshan.query);
        if (len < ANVESHAN_MAX_QUERY - 1) {
            g_anveshan.query[len] = ascii;
            g_anveshan.query[len + 1] = '\0';
            g_anveshan.cursor_pos = len + 1;
            anveshan_query(g_anveshan.query);
        }
        return 1;
    }
    
    return 0;
}

/* ============ State Access ============ */

anveshan_state_t* anveshan_get_state(void) {
    return &g_anveshan;
}

void anveshan_update_settings(const anveshan_settings_t *settings) {
    if (!settings) return;
    g_anveshan.settings = *settings;
    g_anveshan.bar_x = (g_anveshan.screen_width - g_anveshan.settings.width) / 2;
}

int anveshan_is_active(void) {
    return g_anveshan.active;
}
