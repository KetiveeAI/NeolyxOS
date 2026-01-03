/*
 * NeolyxOS Files.app - File View
 * 
 * Renders files in icons, list, columns, or gallery view.
 * 
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>
#include <stddef.h>
#include "nxrender.h"
#include "../../include/nxicon.h"

/* Icon IDs (match resources/icons/*.nxi) */
#define ICON_FOLDER     1
#define ICON_FILE       2
#define ICON_IMAGE      3
#define ICON_DOCUMENT   4
#define ICON_ARCHIVE    5
#define ICON_AUDIO      6
#define ICON_VIDEO      7
#define ICON_CODE       8

/* Forward declarations */
void format_size(uint64_t bytes, char *buf);
void format_time(uint64_t timestamp, char *buf);
static void draw_icon(void *ctx, int icon_id, int x, int y, int size, uint32_t fallback_color);

/* ============ Types ============ */

typedef struct file_entry file_entry_t;  /* From main.c */

typedef enum {
    VIEW_ICONS,
    VIEW_LIST,
    VIEW_COLUMNS,
    VIEW_GALLERY
} view_mode_t;

/* ============ Colors ============ */

#define COLOR_BG            0x1E1E2E
#define COLOR_SELECTED      0x45475A
#define COLOR_HOVER         0x313244
#define COLOR_TEXT          0xCDD6F4
#define COLOR_TEXT_DIM      0x6C7086
#define COLOR_FOLDER        0x89B4FA
#define COLOR_FILE          0xF5C2E7

/* ============ Icon View ============ */

void file_view_draw_icons(void *ctx, file_entry_t *entries, int count, 
                          int icon_size, int x, int y, int width, int height) {
    int padding = 12;
    int cell_width = icon_size + padding * 2;
    int cell_height = icon_size + 32;  /* Icon + label */
    int cols = (width - padding) / cell_width;
    if (cols < 1) cols = 1;
    
    for (int i = 0; i < count; i++) {
        int col = i % cols;
        int row = i / cols;
        
        int cx = x + col * cell_width + padding;
        int cy = y + row * cell_height + padding;
        
        file_entry_t *entry = &entries[i];
        
        /* Selected background */
        if (entry->selected) {
            nx_fill_rect(ctx, cx - 4, cy - 4, cell_width - 4, cell_height - 4, COLOR_SELECTED);
        }
        
        /* Draw icon using icon_id or fallback color */
        uint32_t fallback_color = entry->is_dir ? COLOR_FOLDER : COLOR_FILE;
        int icon_x = cx + (cell_width - icon_size) / 2;
        draw_icon(ctx, entry->icon_id, icon_x, cy, icon_size, fallback_color);
        
        /* Label */
        int label_y = cy + icon_size + 4;
        nx_draw_text(ctx, entry->name, cx + 4, label_y, COLOR_TEXT);
    }
}

/* ============ List View ============ */

void file_view_draw_list(void *ctx, file_entry_t *entries, int count,
                         int x, int y, int width, int height) {
    int row_height = 24;
    int icon_col = x + 8;
    int name_col = x + 40;
    int size_col = x + width - 180;
    int date_col = x + width - 100;
    
    /* Header */
    nx_fill_rect(ctx, x, y, width, row_height, COLOR_HOVER);
    nx_draw_text(ctx, "Name", name_col, y + 4, COLOR_TEXT);
    nx_draw_text(ctx, "Size", size_col, y + 4, COLOR_TEXT);
    nx_draw_text(ctx, "Modified", date_col, y + 4, COLOR_TEXT);
    
    /* Entries */
    for (int i = 0; i < count; i++) {
        int ry = y + (i + 1) * row_height;
        if (ry > y + height) break;
        
        file_entry_t *entry = &entries[i];
        
        /* Selected row */
        if (entry->selected) {
            nx_fill_rect(ctx, x, ry, width, row_height, COLOR_SELECTED);
        }
        
        /* Icon indicator */
        uint32_t icon_color = entry->is_dir ? COLOR_FOLDER : COLOR_FILE;
        nx_fill_rect(ctx, icon_col, ry + 4, 16, 16, icon_color);
        
        /* Name */
        uint32_t text_color = entry->is_hidden ? COLOR_TEXT_DIM : COLOR_TEXT;
        nx_draw_text(ctx, entry->name, name_col, ry + 4, text_color);
        
        /* Size */
        if (!entry->is_dir) {
            char size_str[32];
            format_size(entry->size, size_str);
            nx_draw_text(ctx, size_str, size_col, ry + 4, COLOR_TEXT_DIM);
        }
        
        /* Modified time */
        char time_str[32];
        format_time(entry->mtime, time_str);
        nx_draw_text(ctx, time_str, date_col, ry + 4, COLOR_TEXT_DIM);
    }
}

/* ============ Columns View ============ */

/* Column state for multi-column browser */
static struct {
    char paths[4][1024];
    int columns;
    int scroll[4];
} g_column_state;

void file_view_draw_columns(void *ctx, file_entry_t *entries, int count,
                            int x, int y, int width, int height) {
    /* Multi-column browser (like Finder columns) */
    int column_width = 200;
    int max_columns = (width / column_width);
    if (max_columns < 1) max_columns = 1;
    if (max_columns > 4) max_columns = 4;
    
    int row_height = 22;
    
    /* Draw current directory as first column */
    int col_x = x;
    
    /* Draw column separator */
    nx_fill_rect(ctx, col_x, y, column_width, height, COLOR_BG);
    
    /* Draw entries in column */
    for (int i = 0; i < count && i * row_height < height; i++) {
        int ry = y + i * row_height;
        file_entry_t *entry = &entries[i];
        
        if (entry->selected) {
            nx_fill_rect(ctx, col_x, ry, column_width - 1, row_height, COLOR_SELECTED);
        }
        
        /* Small icon */
        uint32_t icon_color = entry->is_dir ? COLOR_FOLDER : COLOR_FILE;
        nx_fill_rect(ctx, col_x + 4, ry + 3, 16, 16, icon_color);
        
        /* Name */
        nx_draw_text(ctx, entry->name, col_x + 24, ry + 4, COLOR_TEXT);
        
        /* Arrow indicator for directories */
        if (entry->is_dir) {
            nx_draw_text(ctx, ">", col_x + column_width - 16, ry + 4, COLOR_TEXT_DIM);
        }
    }
    
    /* Draw column divider */
    nx_fill_rect(ctx, col_x + column_width - 1, y, 1, height, COLOR_HOVER);
}

/* ============ Gallery View ============ */

void file_view_draw_gallery(void *ctx, file_entry_t *entries, int count,
                            int x, int y, int width, int height) {
    /* Large icons for images/media */
    int preview_size = 128;
    int padding = 16;
    int cell_width = preview_size + padding * 2;
    int cell_height = preview_size + 40;
    int cols = (width - padding) / cell_width;
    if (cols < 1) cols = 1;
    
    for (int i = 0; i < count; i++) {
        int col = i % cols;
        int row = i / cols;
        
        int cx = x + col * cell_width + padding;
        int cy = y + row * cell_height + padding;
        
        file_entry_t *entry = &entries[i];
        
        if (entry->selected) {
            nx_fill_rect(ctx, cx - 4, cy - 4, cell_width - 4, cell_height - 4, COLOR_SELECTED);
        }
        
        /* Preview area */
        nx_fill_rect(ctx, cx, cy, preview_size, preview_size, COLOR_HOVER);
        
        /* Draw thumbnail or icon based on file type */
        if (entry->icon_id == ICON_IMAGE) {
            /* For images, draw a placeholder indicating image preview */
            /* Actual thumbnail loading would require image decoder integration */
            nx_fill_rect(ctx, cx + 4, cy + 4, preview_size - 8, preview_size - 8, 0x585B70);
            /* Draw image indicator in center */
            int cx_center = cx + preview_size / 2 - 16;
            int cy_center = cy + preview_size / 2 - 16;
            nx_fill_rect(ctx, cx_center, cy_center, 32, 32, COLOR_FILE);
        } else {
            /* Non-image: draw centered icon */
            int icon_size = 64;
            int ix = cx + (preview_size - icon_size) / 2;
            int iy = cy + (preview_size - icon_size) / 2;
            uint32_t fallback = entry->is_dir ? COLOR_FOLDER : COLOR_FILE;
            draw_icon(ctx, entry->icon_id, ix, iy, icon_size, fallback);
        }
        
        /* Label */
        nx_draw_text(ctx, entry->name, cx + 4, cy + preview_size + 8, COLOR_TEXT);
    }
}

/* ============ Main Draw Function ============ */

void file_view_draw(void *ctx, file_entry_t *entries, int count,
                    view_mode_t mode, int icon_size,
                    int x, int y, int width, int height) {
    /* Clear background */
    nx_fill_rect(ctx, x, y, width, height, COLOR_BG);
    
    switch (mode) {
        case VIEW_ICONS:
            file_view_draw_icons(ctx, entries, count, icon_size, x, y, width, height);
            break;
        case VIEW_LIST:
            file_view_draw_list(ctx, entries, count, x, y, width, height);
            break;
        case VIEW_COLUMNS:
            file_view_draw_columns(ctx, entries, count, x, y, width, height);
            break;
        case VIEW_GALLERY:
            file_view_draw_gallery(ctx, entries, count, x, y, width, height);
            break;
    }
}

/* ============ Icon Drawing ============ */

/*
 * Draw a directory icon with special folder detection.
 * Uses nxicon API to resolve appropriate icon.
 */
static void draw_dir_icon(void *ctx, const char *dir_path, int x, int y, int size) {
    nxicon_entry_t icon;
    if (nxicon_get_for_directory(dir_path, &icon) == 0 && icon.path[0]) {
        if (nx_draw_nxi(ctx, icon.path, x, y, size, size) == 0) {
            return;
        }
    }
    /* Fallback to generic folder */
    nx_fill_rect(ctx, x, y, size, size, NXICON_COLOR_FOLDER);
    nx_fill_rect(ctx, x, y, size / 3, size / 6, NXICON_COLOR_FOLDER - 0x101010);
}

/*
 * Draw an icon at the specified position.
 * Uses NXI vector icons when available, falls back to colored rectangle.
 */
static void draw_icon(void *ctx, int icon_id, int x, int y, int size, uint32_t fallback_color) {
    /* Try to load and render NXI icon */
    const char *icon_path = NULL;
    
    switch (icon_id) {
        case ICON_FOLDER:   icon_path = NXICON_FOLDER; break;
        case ICON_FILE:     icon_path = NXICON_FILE; break;
        case ICON_IMAGE:    icon_path = NXICON_IMAGE; break;
        case ICON_DOCUMENT: icon_path = NXICON_DOCUMENTS; break;
        case ICON_AUDIO:    icon_path = NXICON_MUSIC; break;
        case ICON_VIDEO:    icon_path = NXICON_VIDEO; break;
        default: break;
    }
    
    if (icon_path) {
        /* Attempt to render NXI icon */
        if (nx_draw_nxi(ctx, icon_path, x, y, size, size) == 0) {
            return;  /* Successfully drew icon */
        }
    }
    
    /* Fallback: draw colored rectangle */
    nx_fill_rect(ctx, x, y, size, size, fallback_color);
    
    /* Draw simple shape indicator */
    if (icon_id == ICON_FOLDER) {
        /* Folder tab */
        nx_fill_rect(ctx, x, y, size / 3, size / 6, fallback_color - 0x101010);
    }
}

/* ============ Utility ============ */

/*
 * Format a Unix timestamp into a human-readable date string.
 * Format: "MMM DD, YYYY" or "HH:MM" if today
 */
void format_time(uint64_t timestamp, char *buf) {
    if (timestamp == 0) {
        buf[0] = '-'; buf[1] = '-'; buf[2] = '\0';
        return;
    }
    
    /* Simple time formatting */
    /* Convert seconds since epoch to approximate date */
    uint64_t days = timestamp / 86400;
    uint64_t years = days / 365;
    uint64_t year = 1970 + years;
    uint64_t day_of_year = days % 365;
    
    /* Approximate month (30-day months) */
    int month = (int)(day_of_year / 30) + 1;
    int day = (int)(day_of_year % 30) + 1;
    
    static const char *months[] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };
    
    if (month > 12) month = 12;
    
    /* Format: "Jan 01, 2025" */
    const char *m = months[month - 1];
    int i = 0;
    buf[i++] = m[0]; buf[i++] = m[1]; buf[i++] = m[2];
    buf[i++] = ' ';
    buf[i++] = '0' + (day / 10);
    buf[i++] = '0' + (day % 10);
    buf[i++] = ',';
    buf[i++] = ' ';
    buf[i++] = '0' + ((year / 1000) % 10);
    buf[i++] = '0' + ((year / 100) % 10);
    buf[i++] = '0' + ((year / 10) % 10);
    buf[i++] = '0' + (year % 10);
    buf[i] = '\0';
}

void format_size(uint64_t bytes, char *buf) {
    if (bytes < 1024) {
        /* bytes */
        int i = 0;
        if (bytes == 0) { buf[i++] = '0'; }
        else {
            char tmp[16];
            int j = 0;
            while (bytes > 0) {
                tmp[j++] = '0' + (bytes % 10);
                bytes /= 10;
            }
            while (j > 0) buf[i++] = tmp[--j];
        }
        buf[i++] = ' '; buf[i++] = 'B'; buf[i] = '\0';
    } else if (bytes < 1024 * 1024) {
        uint64_t kb = bytes / 1024;
        int i = 0;
        char tmp[16];
        int j = 0;
        while (kb > 0) {
            tmp[j++] = '0' + (kb % 10);
            kb /= 10;
        }
        while (j > 0) buf[i++] = tmp[--j];
        buf[i++] = ' '; buf[i++] = 'K'; buf[i++] = 'B'; buf[i] = '\0';
    } else if (bytes < 1024ULL * 1024 * 1024) {
        uint64_t mb = bytes / (1024 * 1024);
        int i = 0;
        char tmp[16];
        int j = 0;
        while (mb > 0) {
            tmp[j++] = '0' + (mb % 10);
            mb /= 10;
        }
        while (j > 0) buf[i++] = tmp[--j];
        buf[i++] = ' '; buf[i++] = 'M'; buf[i++] = 'B'; buf[i] = '\0';
    } else {
        uint64_t gb = bytes / (1024ULL * 1024 * 1024);
        int i = 0;
        char tmp[16];
        int j = 0;
        while (gb > 0) {
            tmp[j++] = '0' + (gb % 10);
            gb /= 10;
        }
        while (j > 0) buf[i++] = tmp[--j];
        buf[i++] = ' '; buf[i++] = 'G'; buf[i++] = 'B'; buf[i] = '\0';
    }
}
