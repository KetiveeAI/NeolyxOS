/*
 * NeolyxOS App Drawer Implementation
 * 
 * Android-style app drawer/launcher showing all installed applications
 * in a grid view. Opens with NX key press.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include "../include/app_drawer.h"
#include "../include/desktop_shell.h"
#include <string.h>

/* ============ External Desktop Functions ============ */

extern void desktop_fill_rect(int32_t x, int32_t y, uint32_t w, uint32_t h, uint32_t color);
extern void desktop_draw_rect(int32_t x, int32_t y, uint32_t w, uint32_t h, uint32_t color);
extern void desktop_draw_text(int32_t x, int32_t y, const char *text, uint32_t color);
extern void fill_rounded_rect(int32_t x, int32_t y, uint32_t w, uint32_t h, int32_t r, uint32_t color);
extern void fill_circle(int32_t cx, int32_t cy, int32_t r, uint32_t color);
extern void fill_rect_alpha(int32_t x, int32_t y, uint32_t w, uint32_t h, uint32_t color);

/* ============ Static State ============ */

static app_drawer_state_t g_drawer;
static uint32_t g_screen_width;
static uint32_t g_screen_height;

/* Animation constants */
#define ANIMATION_DURATION_MS   250
#define DRAWER_MARGIN           40

/* ============ String Helpers ============ */

static int drawer_strlen(const char *s) {
    int len = 0;
    while (s && s[len]) len++;
    return len;
}

static void drawer_strcpy(char *dst, const char *src, int max) {
    int i = 0;
    while (src && src[i] && i < max - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static int drawer_strcasecmp(const char *a, const char *b) {
    while (*a && *b) {
        char ca = (*a >= 'A' && *a <= 'Z') ? *a + 32 : *a;
        char cb = (*b >= 'A' && *b <= 'Z') ? *b + 32 : *b;
        if (ca != cb) return ca - cb;
        a++; b++;
    }
    return *a - *b;
}

static int drawer_strstr(const char *haystack, const char *needle) {
    if (!needle[0]) return 1;
    for (int i = 0; haystack[i]; i++) {
        int j;
        for (j = 0; needle[j]; j++) {
            char h = (haystack[i+j] >= 'A' && haystack[i+j] <= 'Z') ? haystack[i+j] + 32 : haystack[i+j];
            char n = (needle[j] >= 'A' && needle[j] <= 'Z') ? needle[j] + 32 : needle[j];
            if (h != n) break;
        }
        if (!needle[j]) return 1;
    }
    return 0;
}

/* ============ App Discovery ============ */

/* Built-in apps - these are registered automatically */
static const struct {
    const char *name;
    const char *path;
    app_category_t category;
} builtin_apps[] = {
    { "Settings",       "/Applications/Settings.app",       APP_CAT_SYSTEM },
    { "Path",           "/Applications/Path.app",           APP_CAT_SYSTEM },
    { "Terminal",       "/Applications/Terminal.app",       APP_CAT_SYSTEM },
    { "FontManager",    "/Applications/FontManager.app",    APP_CAT_SYSTEM },
    { "Calculator",     "/Applications/Calculator.app",     APP_CAT_UTILITIES },
    { "Calendar",       "/Applications/Calendar.app",       APP_CAT_UTILITIES },
    { "Clock",          "/Applications/Clock.app",          APP_CAT_UTILITIES },
    { "RoleCut",        "/Applications/RoleCut.app",        APP_CAT_MEDIA },
    { "Rivee",          "/Applications/Rivee.app",          APP_CAT_MEDIA },
    { "ReoLab",         "/Applications/reolab.app",         APP_CAT_DEVELOPMENT },
    { NULL, NULL, 0 }
};

int app_drawer_init(uint32_t screen_width, uint32_t screen_height) {
    memset(&g_drawer, 0, sizeof(app_drawer_state_t));
    
    g_screen_width = screen_width;
    g_screen_height = screen_height;
    
    /* Calculate drawer dimensions */
    g_drawer.drawer_width = screen_width - (DRAWER_MARGIN * 2);
    g_drawer.drawer_height = screen_height - (DRAWER_MARGIN * 2) - (MENUBAR_HEIGHT + DOCK_HEIGHT);
    g_drawer.drawer_x = DRAWER_MARGIN;
    g_drawer.drawer_y = MENUBAR_HEIGHT + DRAWER_MARGIN;
    
    /* Grid position inside drawer */
    g_drawer.grid_x = g_drawer.drawer_x + 40;
    g_drawer.grid_y = g_drawer.drawer_y + APP_DRAWER_SEARCH_HEIGHT + 60;
    
    g_drawer.selected_index = -1;
    g_drawer.hover_index = -1;
    g_drawer.current_category = APP_CAT_ALL;
    
    /* Register built-in apps */
    app_drawer_scan_apps();
    
    return 0;
}

int app_drawer_scan_apps(void) {
    g_drawer.app_count = 0;
    
    /* Register built-in apps */
    for (int i = 0; builtin_apps[i].name; i++) {
        app_drawer_register_app(
            builtin_apps[i].name,
            builtin_apps[i].path,
            NULL,  /* Will use default icon based on path */
            builtin_apps[i].category
        );
    }
    
    /* Update filtered list */
    app_drawer_set_category(g_drawer.current_category);
    
    return g_drawer.app_count;
}

int app_drawer_register_app(const char *name, const char *path, 
                            const char *icon_path, app_category_t category) {
    if (g_drawer.app_count >= MAX_APPS) {
        return -1;
    }
    
    app_entry_t *app = &g_drawer.apps[g_drawer.app_count];
    drawer_strcpy(app->name, name, APP_NAME_MAX);
    drawer_strcpy(app->path, path, APP_PATH_MAX);
    
    if (icon_path) {
        drawer_strcpy(app->icon_path, icon_path, APP_PATH_MAX);
    } else {
        /* Build icon path from app path */
        drawer_strcpy(app->icon_path, path, APP_PATH_MAX - 20);
        /* Append /resources/icon.nxi */
    }
    
    app->category = category;
    app->visible = 1;
    app->pinned = 0;
    app->recent = 0;
    
    g_drawer.app_count++;
    return 0;
}

/* ============ Filtering ============ */

static void update_filtered_list(void) {
    g_drawer.filtered_count = 0;
    
    for (uint32_t i = 0; i < g_drawer.app_count; i++) {
        app_entry_t *app = &g_drawer.apps[i];
        
        /* Category filter */
        if (g_drawer.current_category != APP_CAT_ALL && 
            app->category != g_drawer.current_category) {
            continue;
        }
        
        /* Search filter */
        if (g_drawer.search_query[0] && !drawer_strstr(app->name, g_drawer.search_query)) {
            continue;
        }
        
        g_drawer.filtered[g_drawer.filtered_count++] = app;
    }
    
    /* Calculate pages */
    uint32_t apps_per_page = APP_DRAWER_COLS * APP_DRAWER_ROWS;
    g_drawer.total_pages = (g_drawer.filtered_count + apps_per_page - 1) / apps_per_page;
    if (g_drawer.total_pages == 0) g_drawer.total_pages = 1;
    if (g_drawer.current_page >= g_drawer.total_pages) {
        g_drawer.current_page = g_drawer.total_pages - 1;
    }
}

void app_drawer_set_category(app_category_t category) {
    g_drawer.current_category = category;
    g_drawer.current_page = 0;
    update_filtered_list();
}

void app_drawer_search(const char *query) {
    drawer_strcpy(g_drawer.search_query, query, 64);
    g_drawer.current_page = 0;
    update_filtered_list();
}

/* ============ Animation ============ */

void app_drawer_open(void) {
    if (!g_drawer.visible) {
        g_drawer.visible = 1;
        g_drawer.animating = 1;
        g_drawer.animation_progress = 0.0f;
    }
}

void app_drawer_close(void) {
    if (g_drawer.visible) {
        g_drawer.animating = 1;
        /* Animation will set visible=0 when done */
    }
}

void app_drawer_toggle(void) {
    if (g_drawer.visible && !g_drawer.animating) {
        app_drawer_close();
    } else if (!g_drawer.visible) {
        app_drawer_open();
    }
}

int app_drawer_is_open(void) {
    return g_drawer.visible;
}

void app_drawer_update(uint32_t delta_ms) {
    if (!g_drawer.animating) return;
    
    float delta = (float)delta_ms / (float)ANIMATION_DURATION_MS;
    
    if (g_drawer.visible && g_drawer.animation_progress < 1.0f) {
        /* Opening animation */
        g_drawer.animation_progress += delta;
        if (g_drawer.animation_progress >= 1.0f) {
            g_drawer.animation_progress = 1.0f;
            g_drawer.animating = 0;
        }
    } else if (g_drawer.visible && g_drawer.animation_progress >= 1.0f) {
        /* Closing animation */
        g_drawer.animation_progress -= delta;
        if (g_drawer.animation_progress <= 0.0f) {
            g_drawer.animation_progress = 0.0f;
            g_drawer.visible = 0;
            g_drawer.animating = 0;
        }
    }
}

/* ============ Rendering ============ */

/* Draw an app icon with label */
static void render_app_icon(app_entry_t *app, int32_t x, int32_t y, int selected, int hovered) {
    uint32_t bg_color = 0x40404060;    /* Default */
    
    if (selected) {
        bg_color = 0x80007AFF;          /* Blue selection */
    } else if (hovered) {
        bg_color = 0x60606080;          /* Hover highlight */
    }
    
    /* Icon background (rounded square) */
    fill_rounded_rect(x, y, APP_DRAWER_ICON_SIZE, APP_DRAWER_ICON_SIZE, 12, bg_color);
    
    /* Draw first letter as placeholder icon */
    char initial[2] = { app->name[0], '\0' };
    int32_t text_x = x + (APP_DRAWER_ICON_SIZE / 2) - 4;
    int32_t text_y = y + (APP_DRAWER_ICON_SIZE / 2) - 4;
    
    /* Large letter in center */
    uint32_t icon_color = 0xFFFFFFFF;
    
    /* Color based on category */
    switch (app->category) {
        case APP_CAT_SYSTEM:      icon_color = 0xFF007AFF; break;  /* Blue */
        case APP_CAT_PRODUCTIVITY: icon_color = 0xFF34C759; break; /* Green */
        case APP_CAT_MEDIA:       icon_color = 0xFFFF9500; break;  /* Orange */
        case APP_CAT_UTILITIES:   icon_color = 0xFF5856D6; break;  /* Purple */
        case APP_CAT_DEVELOPMENT: icon_color = 0xFFFF2D55; break;  /* Pink */
        case APP_CAT_GAMES:       icon_color = 0xFF00D4AA; break;  /* Teal */
        default:                  icon_color = 0xFF8E8E93; break;  /* Gray */
    }
    
    /* Draw colored circle with initial */
    fill_circle(x + APP_DRAWER_ICON_SIZE/2, y + APP_DRAWER_ICON_SIZE/2, 
                APP_DRAWER_ICON_SIZE/2 - 4, icon_color);
    desktop_draw_text(text_x, text_y, initial, 0xFFFFFFFF);
    
    /* App name below icon (truncated) */
    char display_name[12];
    int name_len = drawer_strlen(app->name);
    if (name_len > 10) {
        for (int i = 0; i < 8; i++) display_name[i] = app->name[i];
        display_name[8] = '.';
        display_name[9] = '.';
        display_name[10] = '\0';
    } else {
        drawer_strcpy(display_name, app->name, 12);
    }
    
    int32_t label_x = x + (APP_DRAWER_ICON_SIZE / 2) - (drawer_strlen(display_name) * 4);
    desktop_draw_text(label_x, y + APP_DRAWER_ICON_SIZE + 4, display_name, 0xFFCCCCCC);
}

/* Render category tabs at top */
static void render_category_tabs(void) {
    int32_t tab_x = g_drawer.drawer_x + 40;
    int32_t tab_y = g_drawer.drawer_y + APP_DRAWER_SEARCH_HEIGHT + 16;
    
    for (int i = 0; i < APP_CAT_COUNT; i++) {
        int text_len = drawer_strlen(APP_CAT_NAMES[i]);
        int tab_width = text_len * 8 + 16;
        
        uint32_t tab_bg = 0x40404060;
        uint32_t tab_text = 0xFFAAAAAA;
        
        if (i == (int)g_drawer.current_category) {
            tab_bg = 0x80007AFF;     /* Blue active */
            tab_text = 0xFFFFFFFF;
        }
        
        fill_rounded_rect(tab_x, tab_y, tab_width, 24, 12, tab_bg);
        desktop_draw_text(tab_x + 8, tab_y + 6, APP_CAT_NAMES[i], tab_text);
        
        tab_x += tab_width + 8;
    }
}

/* Render page dots at bottom */
static void render_page_dots(void) {
    if (g_drawer.total_pages <= 1) return;
    
    int32_t dots_y = g_drawer.drawer_y + g_drawer.drawer_height - 30;
    int32_t total_width = g_drawer.total_pages * 16;
    int32_t dots_x = g_drawer.drawer_x + (g_drawer.drawer_width - total_width) / 2;
    
    for (uint32_t i = 0; i < g_drawer.total_pages && i < APP_DRAWER_PAGE_DOTS; i++) {
        uint32_t dot_color = (i == g_drawer.current_page) ? 0xFFFFFFFF : 0x60FFFFFF;
        fill_circle(dots_x + i * 16 + 4, dots_y + 4, 4, dot_color);
    }
}

/* Render search bar */
static void render_search_bar(void) {
    int32_t bar_x = g_drawer.drawer_x + 80;
    int32_t bar_y = g_drawer.drawer_y + 16;
    uint32_t bar_width = g_drawer.drawer_width - 160;
    
    /* Search bar background */
    fill_rounded_rect(bar_x, bar_y, bar_width, 36, 18, 0x40404060);
    
    /* Search icon (magnifying glass placeholder) */
    desktop_draw_text(bar_x + 12, bar_y + 10, "O", 0xFF888888);
    
    /* Search text or placeholder */
    if (g_drawer.search_query[0]) {
        desktop_draw_text(bar_x + 32, bar_y + 10, g_drawer.search_query, 0xFFFFFFFF);
    } else {
        desktop_draw_text(bar_x + 32, bar_y + 10, "Search apps...", 0xFF888888);
    }
}

void app_drawer_render(void) {
    if (!g_drawer.visible && !g_drawer.animating) {
        return;
    }
    
    /* Apply animation offset (slide up from bottom) */
    float progress = g_drawer.animation_progress;
    int32_t offset_y = (int32_t)((1.0f - progress) * g_drawer.drawer_height);
    
    /* Backdrop (darken screen) */
    uint8_t backdrop_alpha = (uint8_t)(progress * 160);
    fill_rect_alpha(0, 0, g_screen_width, g_screen_height, (backdrop_alpha << 24) | 0x000000);
    
    /* Drawer background */
    int32_t drawer_y = g_drawer.drawer_y + offset_y;
    fill_rounded_rect(g_drawer.drawer_x, drawer_y, 
                      g_drawer.drawer_width, g_drawer.drawer_height,
                      20, APP_DRAWER_BG_COLOR);
    
    /* Handle bar at top (like Android) */
    int32_t handle_x = g_drawer.drawer_x + (g_drawer.drawer_width - 40) / 2;
    fill_rounded_rect(handle_x, drawer_y + 8, 40, 4, 2, 0x60FFFFFF);
    
    /* Only render content when fully open */
    if (progress < 0.9f) return;
    
    /* Search bar */
    render_search_bar();
    
    /* Category tabs */
    render_category_tabs();
    
    /* App grid */
    uint32_t apps_per_page = APP_DRAWER_COLS * APP_DRAWER_ROWS;
    uint32_t start_idx = g_drawer.current_page * apps_per_page;
    uint32_t cell_width = APP_DRAWER_ICON_SIZE + APP_DRAWER_ICON_GAP;
    uint32_t cell_height = APP_DRAWER_ICON_SIZE + APP_DRAWER_LABEL_HEIGHT + APP_DRAWER_ICON_GAP;
    
    for (uint32_t i = 0; i < apps_per_page; i++) {
        uint32_t idx = start_idx + i;
        if (idx >= g_drawer.filtered_count) break;
        
        uint32_t col = i % APP_DRAWER_COLS;
        uint32_t row = i / APP_DRAWER_COLS;
        
        int32_t x = g_drawer.grid_x + col * cell_width;
        int32_t y = g_drawer.grid_y + row * cell_height;
        
        int selected = ((int32_t)idx == g_drawer.selected_index);
        int hovered = ((int32_t)idx == g_drawer.hover_index);
        
        render_app_icon(g_drawer.filtered[idx], x, y, selected, hovered);
    }
    
    /* Page indicator dots */
    render_page_dots();
}

/* ============ Input Handling ============ */

int app_drawer_handle_mouse(int32_t x, int32_t y, uint8_t buttons) {
    if (!g_drawer.visible) return 0;
    
    /* Check if inside drawer */
    if (x < g_drawer.drawer_x || x >= (int32_t)(g_drawer.drawer_x + g_drawer.drawer_width) ||
        y < g_drawer.drawer_y || y >= (int32_t)(g_drawer.drawer_y + g_drawer.drawer_height)) {
        /* Click outside - close drawer */
        if (buttons & 0x01) {
            app_drawer_close();
        }
        return 1;  /* Consume event when drawer is open */
    }
    
    /* Check hover on app icons */
    uint32_t apps_per_page = APP_DRAWER_COLS * APP_DRAWER_ROWS;
    uint32_t start_idx = g_drawer.current_page * apps_per_page;
    uint32_t cell_width = APP_DRAWER_ICON_SIZE + APP_DRAWER_ICON_GAP;
    uint32_t cell_height = APP_DRAWER_ICON_SIZE + APP_DRAWER_LABEL_HEIGHT + APP_DRAWER_ICON_GAP;
    
    g_drawer.hover_index = -1;
    
    for (uint32_t i = 0; i < apps_per_page; i++) {
        uint32_t idx = start_idx + i;
        if (idx >= g_drawer.filtered_count) break;
        
        uint32_t col = i % APP_DRAWER_COLS;
        uint32_t row = i / APP_DRAWER_COLS;
        
        int32_t icon_x = g_drawer.grid_x + col * cell_width;
        int32_t icon_y = g_drawer.grid_y + row * cell_height;
        
        if (x >= icon_x && x < icon_x + (int32_t)APP_DRAWER_ICON_SIZE &&
            y >= icon_y && y < icon_y + (int32_t)APP_DRAWER_ICON_SIZE) {
            g_drawer.hover_index = idx;
            
            /* Click to launch */
            if (buttons & 0x01) {
                app_drawer_launch_app(idx);
                app_drawer_close();
            }
            break;
        }
    }
    
    /* Check category tabs click */
    if (buttons & 0x01) {
        int32_t tab_x = g_drawer.drawer_x + 40;
        int32_t tab_y = g_drawer.drawer_y + APP_DRAWER_SEARCH_HEIGHT + 16;
        
        for (int i = 0; i < APP_CAT_COUNT; i++) {
            int text_len = drawer_strlen(APP_CAT_NAMES[i]);
            int tab_width = text_len * 8 + 16;
            
            if (x >= tab_x && x < tab_x + tab_width &&
                y >= tab_y && y < tab_y + 24) {
                app_drawer_set_category((app_category_t)i);
                break;
            }
            
            tab_x += tab_width + 8;
        }
    }
    
    return 1;  /* Always consume when drawer is open */
}

int app_drawer_handle_key(uint8_t scancode, uint8_t pressed) {
    if (!g_drawer.visible) return 0;
    
    if (!pressed) return 1;  /* Only handle press events */
    
    /* Escape to close */
    if (scancode == 0x01) {  /* ESC */
        app_drawer_close();
        return 1;
    }
    
    /* Arrow keys for navigation */
    if (scancode == 0x48) {  /* Up */
        if (g_drawer.selected_index >= APP_DRAWER_COLS) {
            g_drawer.selected_index -= APP_DRAWER_COLS;
        }
        return 1;
    }
    if (scancode == 0x50) {  /* Down */
        if (g_drawer.selected_index + APP_DRAWER_COLS < (int32_t)g_drawer.filtered_count) {
            g_drawer.selected_index += APP_DRAWER_COLS;
        }
        return 1;
    }
    if (scancode == 0x4B) {  /* Left */
        if (g_drawer.selected_index > 0) {
            g_drawer.selected_index--;
        }
        return 1;
    }
    if (scancode == 0x4D) {  /* Right */
        if (g_drawer.selected_index < (int32_t)g_drawer.filtered_count - 1) {
            g_drawer.selected_index++;
        }
        return 1;
    }
    
    /* Enter to launch */
    if (scancode == 0x1C && g_drawer.selected_index >= 0) {  /* ENTER */
        app_drawer_launch_app(g_drawer.selected_index);
        app_drawer_close();
        return 1;
    }
    
    return 1;  /* Consume all keys when drawer is open */
}

/* ============ App Launch ============ */

int app_drawer_launch_app(uint32_t index) {
    if (index >= g_drawer.filtered_count) {
        return -1;
    }
    
    app_entry_t *app = g_drawer.filtered[index];
    
    /* Mark as recently used */
    app->recent = 1;
    
    /* TODO: Actually launch the app via process manager */
    /* For now, just log */
    /* serial_println("Launching app: "); */
    /* serial_println(app->path); */
    
    return 0;
}

/* ============ State Access ============ */

const app_drawer_state_t* app_drawer_get_state(void) {
    return &g_drawer;
}
