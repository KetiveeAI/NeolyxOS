/*
 * NeolyxOS Files.app - Quick Look Preview
 * 
 * Preview files without opening them, like macOS Quick Look.
 * Press Space to preview selected file.
 * 
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>
#include <stddef.h>
#include "nxrender.h"

/* VFS operations (from vfs_ops.c) */
extern int path_vfs_read(const char *path, void *buf, int size);
extern int path_vfs_list_dir(const char *path, void *entries, int max);

/* Size formatting (from file_view.c) */
extern void format_size(uint64_t bytes, char *buf);

/* ============ Colors ============ */

#define COLOR_QL_BG         0x1E1E2E
#define COLOR_QL_BORDER     0x45475A
#define COLOR_QL_OVERLAY    0x11111B
#define COLOR_QL_TEXT       0xCDD6F4
#define COLOR_QL_TEXT_DIM   0x6C7086
#define COLOR_QL_TOOLBAR    0x181825

/* ============ Preview Types ============ */

typedef enum {
    PREVIEW_NONE,
    PREVIEW_TEXT,
    PREVIEW_IMAGE,
    PREVIEW_AUDIO,
    PREVIEW_VIDEO,
    PREVIEW_PDF,
    PREVIEW_FOLDER,
    PREVIEW_BINARY
} preview_type_t;

/* ============ Quick Look State ============ */

typedef struct {
    int visible;
    int x, y, width, height;
    
    char file_path[1024];
    char file_name[256];
    uint64_t file_size;
    preview_type_t type;
    
    /* Text preview */
    char text_content[4096];
    int text_lines;
    
    /* Image preview */
    void *image_data;
    int image_width;
    int image_height;
    
    /* Folder preview */
    int item_count;
    char items[8][64];  /* First 8 items */
} quick_look_t;

/* ============ Initialization ============ */

void quick_look_init(quick_look_t *ql) {
    ql->visible = 0;
    ql->x = 0;
    ql->y = 0;
    ql->width = 600;
    ql->height = 500;
    ql->file_path[0] = '\0';
    ql->file_name[0] = '\0';
    ql->file_size = 0;
    ql->type = PREVIEW_NONE;
    ql->text_content[0] = '\0';
    ql->text_lines = 0;
    ql->image_data = NULL;
    ql->image_width = 0;
    ql->image_height = 0;
    ql->item_count = 0;
}

/* ============ File Type Detection ============ */

static preview_type_t detect_preview_type(const char *filename) {
    /* Find extension */
    int len = 0;
    while (filename[len]) len++;
    
    int dot = -1;
    for (int i = len - 1; i >= 0; i--) {
        if (filename[i] == '.') { dot = i; break; }
    }
    
    if (dot < 0) return PREVIEW_BINARY;
    
    const char *ext = &filename[dot + 1];
    
    /* Text files */
    if ((ext[0]=='t' && ext[1]=='x' && ext[2]=='t') ||
        (ext[0]=='m' && ext[1]=='d') ||
        (ext[0]=='c' && (ext[1]=='\0' || ext[1]=='p' || ext[1]=='c')) ||
        (ext[0]=='h' && (ext[1]=='\0' || (ext[1]=='p' && ext[2]=='p'))) ||
        (ext[0]=='j' && ext[1]=='s') ||
        (ext[0]=='p' && ext[1]=='y') ||
        (ext[0]=='r' && ext[1]=='s') ||
        (ext[0]=='s' && ext[1]=='h') ||
        (ext[0]=='j' && ext[1]=='s' && ext[2]=='o' && ext[3]=='n') ||
        (ext[0]=='x' && ext[1]=='m' && ext[2]=='l') ||
        (ext[0]=='h' && ext[1]=='t' && ext[2]=='m' && ext[3]=='l') ||
        (ext[0]=='c' && ext[1]=='s' && ext[2]=='s')) {
        return PREVIEW_TEXT;
    }
    
    /* Images */
    if ((ext[0]=='p' && ext[1]=='n' && ext[2]=='g') ||
        (ext[0]=='j' && ext[1]=='p' && ext[2]=='g') ||
        (ext[0]=='j' && ext[1]=='p' && ext[2]=='e' && ext[3]=='g') ||
        (ext[0]=='g' && ext[1]=='i' && ext[2]=='f') ||
        (ext[0]=='b' && ext[1]=='m' && ext[2]=='p') ||
        (ext[0]=='s' && ext[1]=='v' && ext[2]=='g') ||
        (ext[0]=='w' && ext[1]=='e' && ext[2]=='b' && ext[3]=='p')) {
        return PREVIEW_IMAGE;
    }
    
    /* Audio */
    if ((ext[0]=='m' && ext[1]=='p' && ext[2]=='3') ||
        (ext[0]=='w' && ext[1]=='a' && ext[2]=='v') ||
        (ext[0]=='f' && ext[1]=='l' && ext[2]=='a' && ext[3]=='c') ||
        (ext[0]=='o' && ext[1]=='g' && ext[2]=='g') ||
        (ext[0]=='m' && ext[1]=='4' && ext[2]=='a')) {
        return PREVIEW_AUDIO;
    }
    
    /* Video */
    if ((ext[0]=='m' && ext[1]=='p' && ext[2]=='4') ||
        (ext[0]=='m' && ext[1]=='k' && ext[2]=='v') ||
        (ext[0]=='a' && ext[1]=='v' && ext[2]=='i') ||
        (ext[0]=='m' && ext[1]=='o' && ext[2]=='v') ||
        (ext[0]=='w' && ext[1]=='e' && ext[2]=='b' && ext[3]=='m')) {
        return PREVIEW_VIDEO;
    }
    
    /* PDF */
    if (ext[0]=='p' && ext[1]=='d' && ext[2]=='f') {
        return PREVIEW_PDF;
    }
    
    return PREVIEW_BINARY;
}

/* ============ Show/Hide ============ */

void quick_look_show(quick_look_t *ql, const char *path, const char *name, 
                     uint64_t size, int is_dir, int screen_w, int screen_h) {
    ql->visible = 1;
    
    /* Copy path and name */
    int i = 0;
    while (path && path[i] && i < 1023) { ql->file_path[i] = path[i]; i++; }
    ql->file_path[i] = '\0';
    
    i = 0;
    while (name && name[i] && i < 255) { ql->file_name[i] = name[i]; i++; }
    ql->file_name[i] = '\0';
    
    ql->file_size = size;
    
    /* Center on screen */
    ql->x = (screen_w - ql->width) / 2;
    ql->y = (screen_h - ql->height) / 2;
    
    /* Detect type */
    if (is_dir) {
        ql->type = PREVIEW_FOLDER;
        /* Load folder contents - first 8 items */
        ql->item_count = 0;
        
        /* Use a simple file entry struct for listing */
        struct { char name[256]; char path[1024]; uint64_t size; uint64_t mtime; uint32_t mode; uint8_t is_dir; uint8_t is_hidden; uint8_t is_symlink; uint8_t selected; uint32_t icon_id; } temp_entries[8];
        
        int count = path_vfs_list_dir(path, temp_entries, 8);
        if (count > 0) {
            ql->item_count = count < 8 ? count : 8;
            for (int j = 0; j < ql->item_count; j++) {
                int k = 0;
                while (temp_entries[j].name[k] && k < 63) {
                    ql->items[j][k] = temp_entries[j].name[k];
                    k++;
                }
                ql->items[j][k] = '\0';
            }
        }
    } else {
        ql->type = detect_preview_type(name);
        
        /* Load content based on type */
        switch (ql->type) {
            case PREVIEW_TEXT: {
                /* Read first 4KB of text file */
                int bytes_read = path_vfs_read(path, ql->text_content, 4095);
                if (bytes_read > 0) {
                    ql->text_content[bytes_read] = '\0';
                    /* Count lines */
                    ql->text_lines = 1;
                    for (int j = 0; j < bytes_read; j++) {
                        if (ql->text_content[j] == '\n') ql->text_lines++;
                    }
                } else {
                    ql->text_content[0] = '\0';
                    ql->text_lines = 0;
                }
                break;
            }
                
            case PREVIEW_IMAGE:
                /* Image loading requires decoder integration */
                /* For now, just mark that we have an image to preview */
                ql->image_data = NULL;  /* Would be decoded image data */
                ql->image_width = 0;
                ql->image_height = 0;
                /* Future: decode PNG/JPEG and store in image_data */
                break;
                
            default:
                break;
        }
    }
}

void quick_look_hide(quick_look_t *ql) {
    ql->visible = 0;
}

void quick_look_toggle(quick_look_t *ql, const char *path, const char *name,
                       uint64_t size, int is_dir, int screen_w, int screen_h) {
    if (ql->visible) {
        quick_look_hide(ql);
    } else {
        quick_look_show(ql, path, name, size, is_dir, screen_w, screen_h);
    }
}

/* ============ Drawing ============ */

void quick_look_draw(void *ctx, quick_look_t *ql) {
    if (!ql->visible) return;
    
    int x = ql->x;
    int y = ql->y;
    int w = ql->width;
    int h = ql->height;
    
    /* Overlay background */
    /* Note: In real impl, draw semi-transparent overlay behind */
    
    /* Panel shadow */
    nx_fill_rect(ctx, x + 8, y + 8, w, h, 0x11111B);
    
    /* Panel background */
    nx_fill_rect(ctx, x, y, w, h, COLOR_QL_BG);
    
    /* Border */
    nx_draw_rect(ctx, x, y, w, h, COLOR_QL_BORDER);
    
    /* Title bar */
    int title_h = 36;
    nx_fill_rect(ctx, x, y, w, title_h, COLOR_QL_TOOLBAR);
    nx_fill_rect(ctx, x, y + title_h, w, 1, COLOR_QL_BORDER);
    
    /* File name */
    nx_draw_text(ctx, ql->file_name, x + 12, y + 10, COLOR_QL_TEXT);
    
    /* Close button */
    nx_draw_text(ctx, "X", x + w - 24, y + 10, COLOR_QL_TEXT_DIM);
    
    /* Content area */
    int content_y = y + title_h + 8;
    int content_h = h - title_h - 60;
    
    switch (ql->type) {
        case PREVIEW_TEXT:
            /* Draw text content */
            if (ql->text_content[0]) {
                nx_draw_text(ctx, ql->text_content, x + 12, content_y, COLOR_QL_TEXT);
            } else {
                nx_draw_text(ctx, "(Text preview)", x + w/2 - 50, content_y + content_h/2, COLOR_QL_TEXT_DIM);
            }
            break;
            
        case PREVIEW_IMAGE:
            /* Draw image or placeholder */
            if (ql->image_data) {
                /* Draw decoded image using NXRender */
                /* nx_draw_image(ctx, ql->image_data, x + 50, content_y, ql->image_width, ql->image_height); */
                /* For now, show dimensions if available */
                char dim_str[64];
                int di = 0;
                dim_str[di++] = '[';
                /* Add width */
                if (ql->image_width > 0) {
                    int w = ql->image_width;
                    char tmp[16]; int ti = 0;
                    while (w > 0) { tmp[ti++] = '0' + (w % 10); w /= 10; }
                    while (ti > 0) dim_str[di++] = tmp[--ti];
                } else { dim_str[di++] = '?'; }
                dim_str[di++] = ' '; dim_str[di++] = 'x'; dim_str[di++] = ' ';
                /* Add height */
                if (ql->image_height > 0) {
                    int h = ql->image_height;
                    char tmp[16]; int ti = 0;
                    while (h > 0) { tmp[ti++] = '0' + (h % 10); h /= 10; }
                    while (ti > 0) dim_str[di++] = tmp[--ti];
                } else { dim_str[di++] = '?'; }
                dim_str[di++] = ']'; dim_str[di] = '\0';
                nx_fill_rect(ctx, x + 50, content_y, w - 100, content_h, 0x313244);
                nx_draw_text(ctx, dim_str, x + w/2 - 40, content_y + content_h/2, COLOR_QL_TEXT_DIM);
            } else {
                nx_fill_rect(ctx, x + 50, content_y, w - 100, content_h, 0x313244);
                nx_draw_text(ctx, "[Image Preview]", x + w/2 - 60, content_y + content_h/2, COLOR_QL_TEXT_DIM);
            }
            break;
            
        case PREVIEW_FOLDER:
            nx_draw_text(ctx, "Folder Contents:", x + 12, content_y, COLOR_QL_TEXT);
            for (int i = 0; i < ql->item_count && i < 8; i++) {
                nx_draw_text(ctx, ql->items[i], x + 24, content_y + 24 + i * 20, COLOR_QL_TEXT_DIM);
            }
            break;
            
        case PREVIEW_AUDIO:
            nx_draw_text(ctx, "[Audio File]", x + w/2 - 50, content_y + content_h/2, COLOR_QL_TEXT_DIM);
            break;
            
        case PREVIEW_VIDEO:
            nx_fill_rect(ctx, x + 50, content_y, w - 100, content_h, 0x000000);
            nx_draw_text(ctx, "[Video Preview]", x + w/2 - 55, content_y + content_h/2, COLOR_QL_TEXT_DIM);
            break;
            
        case PREVIEW_PDF:
            nx_draw_text(ctx, "[PDF Document]", x + w/2 - 55, content_y + content_h/2, COLOR_QL_TEXT_DIM);
            break;
            
        default:
            nx_draw_text(ctx, "No preview available", x + w/2 - 70, content_y + content_h/2, COLOR_QL_TEXT_DIM);
            break;
    }
    
    /* Footer with file info */
    int footer_y = y + h - 44;
    nx_fill_rect(ctx, x, footer_y, w, 44, COLOR_QL_TOOLBAR);
    nx_fill_rect(ctx, x, footer_y, w, 1, COLOR_QL_BORDER);
    
    /* File size */
    char size_str[48] = "Size: ";
    char size_buf[32];
    format_size(ql->file_size, size_buf);
    /* Append size to label */
    int si = 6;  /* After "Size: " */
    int bi = 0;
    while (size_buf[bi] && si < 47) {
        size_str[si++] = size_buf[bi++];
    }
    size_str[si] = '\0';
    nx_draw_text(ctx, size_str, x + 12, footer_y + 14, COLOR_QL_TEXT_DIM);
    
    /* Open button */
    int btn_w = 80;
    int btn_x = x + w - btn_w - 12;
    nx_fill_rect(ctx, btn_x, footer_y + 8, btn_w, 28, 0x89B4FA);
    nx_draw_text(ctx, "Open", btn_x + 22, footer_y + 14, 0x1E1E2E);
}

/* ============ Interaction ============ */

int quick_look_on_key(quick_look_t *ql, uint16_t keycode) {
    if (!ql->visible) return 0;
    
    if (keycode == ' ' || keycode == 0x1B) {  /* Space or Escape */
        quick_look_hide(ql);
        return 1;
    }
    
    return 0;
}

int quick_look_on_click(quick_look_t *ql, int mx, int my) {
    if (!ql->visible) return 0;
    
    /* Click outside closes */
    if (mx < ql->x || mx > ql->x + ql->width ||
        my < ql->y || my > ql->y + ql->height) {
        quick_look_hide(ql);
        return 1;
    }
    
    /* Close button */
    if (mx > ql->x + ql->width - 32 && my < ql->y + 36) {
        quick_look_hide(ql);
        return 1;
    }
    
    /* Open button */
    int footer_y = ql->y + ql->height - 44;
    int btn_x = ql->x + ql->width - 92;
    if (mx >= btn_x && mx <= btn_x + 80 && my >= footer_y + 8 && my <= footer_y + 36) {
        /* Return 2 to signal "open file" action to caller */
        /* Caller (main.c) will handle actual file opening */
        quick_look_hide(ql);
        return 2;  /* Open action - caller handles via file path */
    }
    
    return 0;
}
