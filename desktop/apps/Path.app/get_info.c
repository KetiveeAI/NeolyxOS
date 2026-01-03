/*
 * NeolyxOS Files.app - Get Info Panel
 * 
 * Displays detailed file/folder information like macOS Get Info.
 * 
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>
#include <stddef.h>
#include "nxrender.h"

/* ============ Colors ============ */

#define COLOR_INFO_BG       0x1E1E2E
#define COLOR_INFO_HEADER   0x181825
#define COLOR_INFO_SECTION  0x313244
#define COLOR_INFO_BORDER   0x45475A
#define COLOR_INFO_TEXT     0xCDD6F4
#define COLOR_INFO_LABEL    0x6C7086
#define COLOR_INFO_ICON     0x89B4FA

/* ============ Get Info State ============ */

typedef struct {
    int visible;
    int x, y, width, height;
    
    /* Basic info */
    char name[256];
    char path[1024];
    char kind[64];
    uint64_t size;
    uint64_t size_on_disk;
    int is_dir;
    int is_hidden;
    int is_symlink;
    
    /* Dates */
    uint64_t created;
    uint64_t modified;
    uint64_t accessed;
    
    /* Permissions */
    uint32_t mode;
    char owner[64];
    char group[64];
    
    /* Folder specific */
    int item_count;
    
    /* Expanded sections */
    int show_general;
    int show_permissions;
    int show_dates;
} get_info_t;

/* ============ Initialization ============ */

void get_info_init(get_info_t *info) {
    info->visible = 0;
    info->x = 100;
    info->y = 100;
    info->width = 320;
    info->height = 480;
    info->name[0] = '\0';
    info->path[0] = '\0';
    info->kind[0] = '\0';
    info->size = 0;
    info->size_on_disk = 0;
    info->is_dir = 0;
    info->is_hidden = 0;
    info->is_symlink = 0;
    info->created = 0;
    info->modified = 0;
    info->accessed = 0;
    info->mode = 0;
    info->owner[0] = '\0';
    info->group[0] = '\0';
    info->item_count = 0;
    info->show_general = 1;
    info->show_permissions = 1;
    info->show_dates = 1;
}

/* ============ Show/Hide ============ */

void get_info_show(get_info_t *info, const char *name, const char *path,
                   uint64_t size, int is_dir, uint32_t mode,
                   uint64_t created, uint64_t modified) {
    info->visible = 1;
    
    /* Copy name */
    int i = 0;
    while (name && name[i] && i < 255) { info->name[i] = name[i]; i++; }
    info->name[i] = '\0';
    
    /* Copy path */
    i = 0;
    while (path && path[i] && i < 1023) { info->path[i] = path[i]; i++; }
    info->path[i] = '\0';
    
    info->size = size;
    info->size_on_disk = ((size + 4095) / 4096) * 4096;
    info->is_dir = is_dir;
    info->mode = mode;
    info->created = created;
    info->modified = modified;
    
    /* Determine kind */
    if (is_dir) {
        info->kind[0] = 'F'; info->kind[1] = 'o';
        info->kind[2] = 'l'; info->kind[3] = 'd';
        info->kind[4] = 'e'; info->kind[5] = 'r';
        info->kind[6] = '\0';
    } else {
        /* Find extension */
        int dot = -1;
        for (int j = 0; name[j]; j++) {
            if (name[j] == '.') dot = j;
        }
        if (dot >= 0) {
            info->kind[0] = name[dot+1];
            info->kind[1] = name[dot+2];
            info->kind[2] = name[dot+3];
            info->kind[3] = ' ';
            info->kind[4] = 'F';
            info->kind[5] = 'i';
            info->kind[6] = 'l';
            info->kind[7] = 'e';
            info->kind[8] = '\0';
        } else {
            info->kind[0] = 'F'; info->kind[1] = 'i';
            info->kind[2] = 'l'; info->kind[3] = 'e';
            info->kind[4] = '\0';
        }
    }
    
    /* Default owner/group */
    info->owner[0] = 'u'; info->owner[1] = 's';
    info->owner[2] = 'e'; info->owner[3] = 'r';
    info->owner[4] = '\0';
    info->group[0] = 's'; info->group[1] = 't';
    info->group[2] = 'a'; info->group[3] = 'f';
    info->group[4] = 'f'; info->group[5] = '\0';
}

void get_info_hide(get_info_t *info) {
    info->visible = 0;
}

/* ============ Drawing ============ */

static void draw_section_header(void *ctx, const char *title, int x, int y, int w, int expanded) {
    nx_fill_rect(ctx, x, y, w, 24, COLOR_INFO_SECTION);
    nx_draw_text(ctx, expanded ? "v" : ">", x + 8, y + 4, COLOR_INFO_LABEL);
    nx_draw_text(ctx, title, x + 24, y + 4, COLOR_INFO_TEXT);
}

static void draw_info_row(void *ctx, const char *label, const char *value, int x, int y) {
    nx_draw_text(ctx, label, x + 8, y, COLOR_INFO_LABEL);
    nx_draw_text(ctx, value, x + 100, y, COLOR_INFO_TEXT);
}

void get_info_draw(void *ctx, get_info_t *info) {
    if (!info->visible) return;
    
    int x = info->x;
    int y = info->y;
    int w = info->width;
    int h = info->height;
    
    /* Shadow */
    nx_fill_rect(ctx, x + 6, y + 6, w, h, 0x11111B);
    
    /* Background */
    nx_fill_rect(ctx, x, y, w, h, COLOR_INFO_BG);
    nx_draw_rect(ctx, x, y, w, h, COLOR_INFO_BORDER);
    
    /* Header */
    int header_h = 80;
    nx_fill_rect(ctx, x, y, w, header_h, COLOR_INFO_HEADER);
    nx_fill_rect(ctx, x, y + header_h, w, 1, COLOR_INFO_BORDER);
    
    /* Icon placeholder */
    int icon_size = 48;
    int icon_x = x + (w - icon_size) / 2;
    int icon_y = y + 8;
    nx_fill_rect(ctx, icon_x, icon_y, icon_size, icon_size, COLOR_INFO_ICON);
    
    /* Name (editable style) */
    int name_y = y + 60;
    nx_draw_text(ctx, info->name, x + 8, name_y, COLOR_INFO_TEXT);
    
    int cy = y + header_h + 8;
    
    /* General section */
    draw_section_header(ctx, "General", x, cy, w, info->show_general);
    cy += 28;
    
    if (info->show_general) {
        draw_info_row(ctx, "Kind:", info->kind, x, cy); cy += 20;
        
        /* Size */
        char size_str[32];
        if (info->size < 1024) {
            size_str[0] = '0' + (info->size % 10);
            size_str[1] = ' '; size_str[2] = 'B';
            size_str[3] = '\0';
        } else {
            /* Simplified */
            size_str[0] = '~'; size_str[1] = ' ';
            size_str[2] = 'K'; size_str[3] = 'B';
            size_str[4] = '\0';
        }
        draw_info_row(ctx, "Size:", size_str, x, cy); cy += 20;
        draw_info_row(ctx, "Where:", info->path, x, cy); cy += 20;
        
        cy += 8;
    }
    
    /* Dates section */
    draw_section_header(ctx, "Dates", x, cy, w, info->show_dates);
    cy += 28;
    
    if (info->show_dates) {
        draw_info_row(ctx, "Created:", "-", x, cy); cy += 20;
        draw_info_row(ctx, "Modified:", "-", x, cy); cy += 20;
        draw_info_row(ctx, "Accessed:", "-", x, cy); cy += 20;
        cy += 8;
    }
    
    /* Permissions section */
    draw_section_header(ctx, "Permissions", x, cy, w, info->show_permissions);
    cy += 28;
    
    if (info->show_permissions) {
        draw_info_row(ctx, "Owner:", info->owner, x, cy); cy += 20;
        draw_info_row(ctx, "Group:", info->group, x, cy); cy += 20;
        
        /* Permissions bits */
        char perm_str[16];
        perm_str[0] = (info->mode & 0400) ? 'r' : '-';
        perm_str[1] = (info->mode & 0200) ? 'w' : '-';
        perm_str[2] = (info->mode & 0100) ? 'x' : '-';
        perm_str[3] = (info->mode & 040) ? 'r' : '-';
        perm_str[4] = (info->mode & 020) ? 'w' : '-';
        perm_str[5] = (info->mode & 010) ? 'x' : '-';
        perm_str[6] = (info->mode & 04) ? 'r' : '-';
        perm_str[7] = (info->mode & 02) ? 'w' : '-';
        perm_str[8] = (info->mode & 01) ? 'x' : '-';
        perm_str[9] = '\0';
        draw_info_row(ctx, "Mode:", perm_str, x, cy); cy += 20;
    }
}

/* ============ Interaction ============ */

int get_info_on_click(get_info_t *info, int mx, int my) {
    if (!info->visible) return 0;
    
    /* Outside click closes */
    if (mx < info->x || mx > info->x + info->width ||
        my < info->y || my > info->y + info->height) {
        get_info_hide(info);
        return 1;
    }
    
    /* Section toggle */
    int cy = info->y + 80 + 8;
    
    if (my >= cy && my < cy + 24) {
        info->show_general = !info->show_general;
        return 1;
    }
    cy += 28 + (info->show_general ? 68 : 0);
    
    if (my >= cy && my < cy + 24) {
        info->show_dates = !info->show_dates;
        return 1;
    }
    cy += 28 + (info->show_dates ? 68 : 0);
    
    if (my >= cy && my < cy + 24) {
        info->show_permissions = !info->show_permissions;
        return 1;
    }
    
    return 0;
}

int get_info_on_key(get_info_t *info, uint16_t keycode) {
    if (!info->visible) return 0;
    
    if (keycode == 0x1B) {  /* Escape */
        get_info_hide(info);
        return 1;
    }
    
    return 0;
}
