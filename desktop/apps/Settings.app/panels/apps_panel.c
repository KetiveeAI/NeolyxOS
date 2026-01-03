/*
 * Settings - Apps Panel
 * Displays and manages installed applications
 * 
 * Scans /System/Applications/ and /Applications/
 * Reads manifest.npa from each app for metadata
 * 
 * Copyright (c) 2025 KetiveeAI. All Rights Reserved.
 */

#include "panel_common.h"
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

/* ============ App Categories ============ */

typedef enum {
    APP_CAT_ALL,
    APP_CAT_SYSTEM,
    APP_CAT_PRODUCTIVITY,
    APP_CAT_DEVELOPMENT,
    APP_CAT_GRAPHICS,
    APP_CAT_GAMES,
    APP_CAT_UTILITIES,
    APP_CAT_THIRDPARTY,
    APP_CAT_COUNT
} app_category_t;

static const char *category_names[] = {
    "All Applications",
    "System",
    "Productivity",
    "Development",
    "Graphics",
    "Games",
    "Utilities",
    "Third-Party"
};

/* ============ App Info ============ */

typedef struct {
    char name[64];
    char version[16];
    char bundle_id[128];
    char path[256];
    char icon_path[256];
    char description[256];
    char tagline[128];
    app_category_t category;
    uint64_t size_bytes;
    bool is_system_app;
} app_info_t;

#define MAX_APPS 64
static app_info_t installed_apps[MAX_APPS];
static int app_count = 0;
static app_category_t current_filter = APP_CAT_ALL;

/* ============ Forward Declarations ============ */

extern int vfs_readdir(const char *path, void (*callback)(const char *name, bool is_dir, void *ctx), void *ctx);
extern int vfs_stat(const char *path, uint64_t *size_out);
extern int vfs_read(const char *path, char *buf, uint32_t max_len);

/* ============ Manifest Parsing ============ */

static const char *json_get_string(const char *json, const char *key, char *out, int max_len) {
    /* Simple JSON string extraction - not a full parser */
    char search[128];
    snprintf(search, sizeof(search), "\"%s\":", key);
    
    const char *pos = strstr(json, search);
    if (!pos) return NULL;
    
    pos = strchr(pos, ':');
    if (!pos) return NULL;
    pos++;
    
    while (*pos == ' ' || *pos == '\t') pos++;
    if (*pos != '"') return NULL;
    pos++;
    
    int i = 0;
    while (*pos && *pos != '"' && i < max_len - 1) {
        out[i++] = *pos++;
    }
    out[i] = '\0';
    
    return out;
}

static app_category_t parse_category(const char *cat_str) {
    if (!cat_str) return APP_CAT_UTILITIES;
    
    if (strcmp(cat_str, "System") == 0) return APP_CAT_SYSTEM;
    if (strcmp(cat_str, "Productivity") == 0) return APP_CAT_PRODUCTIVITY;
    if (strcmp(cat_str, "Development") == 0) return APP_CAT_DEVELOPMENT;
    if (strcmp(cat_str, "Graphics") == 0) return APP_CAT_GRAPHICS;
    if (strcmp(cat_str, "Games") == 0) return APP_CAT_GAMES;
    if (strcmp(cat_str, "Utilities") == 0) return APP_CAT_UTILITIES;
    
    return APP_CAT_THIRDPARTY;
}

static int load_app_manifest(const char *app_path, app_info_t *info) {
    char manifest_path[512];
    snprintf(manifest_path, sizeof(manifest_path), "%s/manifest.npa", app_path);
    
    char manifest_data[4096] = {0};
    if (vfs_read(manifest_path, manifest_data, sizeof(manifest_data)) < 0) {
        return -1;
    }
    
    /* Parse manifest */
    char cat_str[32] = {0};
    
    json_get_string(manifest_data, "name", info->name, sizeof(info->name));
    json_get_string(manifest_data, "version", info->version, sizeof(info->version));
    json_get_string(manifest_data, "bundle_id", info->bundle_id, sizeof(info->bundle_id));
    json_get_string(manifest_data, "description", info->description, sizeof(info->description));
    json_get_string(manifest_data, "tagline", info->tagline, sizeof(info->tagline));
    json_get_string(manifest_data, "category", cat_str, sizeof(cat_str));
    
    info->category = parse_category(cat_str);
    
    /* Icon path */
    char icon_rel[128] = {0};
    if (json_get_string(manifest_data, "icon", icon_rel, sizeof(icon_rel))) {
        snprintf(info->icon_path, sizeof(info->icon_path), "%s/%s", app_path, icon_rel);
    }
    
    strncpy(info->path, app_path, sizeof(info->path) - 1);
    
    return 0;
}

/* ============ Directory Scanning ============ */

typedef struct {
    const char *base_path;
    bool is_system;
} scan_context_t;

static void on_app_found(const char *name, bool is_dir, void *ctx) {
    if (!is_dir) return;
    if (app_count >= MAX_APPS) return;
    
    /* Check for .app extension */
    size_t len = strlen(name);
    if (len < 5 || strcmp(name + len - 4, ".app") != 0) return;
    
    scan_context_t *scan = (scan_context_t *)ctx;
    
    char full_path[512];
    snprintf(full_path, sizeof(full_path), "%s/%s", scan->base_path, name);
    
    app_info_t *info = &installed_apps[app_count];
    memset(info, 0, sizeof(app_info_t));
    
    if (load_app_manifest(full_path, info) == 0) {
        info->is_system_app = scan->is_system;
        
        /* Get app size */
        vfs_stat(full_path, &info->size_bytes);
        
        app_count++;
    }
}

static void scan_applications(void) {
    app_count = 0;
    
    /* Scan system applications */
    scan_context_t sys_ctx = {"/System/Applications", true};
    vfs_readdir("/System/Applications", on_app_found, &sys_ctx);
    
    /* Scan user applications */
    scan_context_t user_ctx = {"/Applications", false};
    vfs_readdir("/Applications", on_app_found, &user_ctx);
}

/* ============ Panel Rendering ============ */

static void draw_category_filter(settings_panel_t *panel, int y) {
    int x = panel->x + 20;
    
    panel_draw_text(x, y, "Filter:", 0x888888);
    x += 60;
    
    for (int i = 0; i < APP_CAT_COUNT; i++) {
        uint32_t color = (current_filter == i) ? 0x007AFF : 0x444444;
        panel_draw_button(x, y, category_names[i], color);
        x += 100;
        
        if (x > panel->x + panel->width - 100) {
            x = panel->x + 80;
            y += 30;
        }
    }
}

static void draw_app_list(settings_panel_t *panel, int start_y) {
    int y = start_y;
    int card_height = 80;
    int padding = 10;
    
    for (int i = 0; i < app_count; i++) {
        app_info_t *app = &installed_apps[i];
        
        /* Filter by category */
        if (current_filter != APP_CAT_ALL && app->category != current_filter) {
            continue;
        }
        
        /* Draw app card */
        int card_x = panel->x + 20;
        int card_y = y;
        int card_w = panel->width - 40;
        
        /* Card background */
        panel_fill_rect(card_x, card_y, card_w, card_height, 0x2A2A2A);
        panel_draw_rect(card_x, card_y, card_w, card_height, 0x3A3A3A);
        
        /* Icon placeholder */
        panel_fill_rect(card_x + padding, card_y + padding, 60, 60, 0x444444);
        panel_draw_text(card_x + padding + 10, card_y + padding + 25, "Icon", 0x666666);
        
        /* App info */
        int text_x = card_x + padding + 70;
        
        /* Name and version */
        char title[96];
        snprintf(title, sizeof(title), "%s v%s", app->name, app->version);
        panel_draw_text(text_x, card_y + padding + 5, title, 0xFFFFFF);
        
        /* Tagline or description */
        const char *desc = app->tagline[0] ? app->tagline : app->description;
        panel_draw_text(text_x, card_y + padding + 25, desc, 0x888888);
        
        /* Category badge */
        const char *cat = category_names[app->category];
        uint32_t badge_color = app->is_system_app ? 0x007AFF : 0x34C759;
        panel_draw_badge(text_x, card_y + padding + 45, cat, badge_color);
        
        /* Size */
        char size_str[32];
        if (app->size_bytes >= 1024 * 1024) {
            snprintf(size_str, sizeof(size_str), "%.1f MB", app->size_bytes / (1024.0 * 1024.0));
        } else {
            snprintf(size_str, sizeof(size_str), "%.1f KB", app->size_bytes / 1024.0);
        }
        panel_draw_text(card_x + card_w - 80, card_y + padding + 5, size_str, 0x666666);
        
        y += card_height + 10;
        
        /* Stop if we're past the panel bounds */
        if (y > panel->y + panel->height - card_height) {
            break;
        }
    }
}

/* ============ Panel Callbacks ============ */

void apps_panel_init(settings_panel_t *panel) {
    panel->title = "Applications";
    panel->icon = "apps";
    
    /* Scan installed applications */
    scan_applications();
}

void apps_panel_render(settings_panel_t *panel) {
    int y = panel->y + 20;
    
    /* Header */
    panel_draw_header(panel->x + 20, y, "Installed Applications");
    y += 40;
    
    /* App count */
    char count_str[64];
    snprintf(count_str, sizeof(count_str), "%d applications installed", app_count);
    panel_draw_text(panel->x + 20, y, count_str, 0x888888);
    y += 30;
    
    /* Category filter */
    draw_category_filter(panel, y);
    y += 60;
    
    /* Separator */
    panel_draw_line(panel->x + 20, y, panel->x + panel->width - 20, y, 0x3A3A3A);
    y += 20;
    
    /* App list */
    draw_app_list(panel, y);
}

void apps_panel_handle_event(settings_panel_t *panel, settings_event_t *event) {
    (void)panel;
    
    if (event->type == EVT_CLICK) {
        /* Check category filter clicks */
        int filter_y = panel->y + 90;
        
        if (event->y >= filter_y && event->y <= filter_y + 30) {
            int x = panel->x + 80;
            for (int i = 0; i < APP_CAT_COUNT; i++) {
                if (event->x >= x && event->x <= x + 90) {
                    current_filter = (app_category_t)i;
                    panel->needs_redraw = true;
                    break;
                }
                x += 100;
            }
        }
    }
}

void apps_panel_cleanup(settings_panel_t *panel) {
    (void)panel;
    /* Nothing to clean up */
}
