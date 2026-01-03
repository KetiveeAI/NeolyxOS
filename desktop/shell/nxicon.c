/*
 * NeolyxOS Icon Management Implementation
 * 
 * Handles loading, caching, and custom icon support.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include "../include/nxicon.h"
#include <string.h>

/* ============ String Helpers ============ */

static int nxi_strlen(const char *s) {
    int len = 0;
    while (s && *s++) len++;
    return len;
}

static void nxi_strcpy(char *dst, const char *src, int max) {
    int i = 0;
    while (src && src[i] && i < max - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static int nxi_strcmp(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return *a - *b;
}

static int nxi_ends_with(const char *str, const char *suffix) {
    int str_len = nxi_strlen(str);
    int suf_len = nxi_strlen(suffix);
    if (suf_len > str_len) return 0;
    return nxi_strcmp(str + str_len - suf_len, suffix) == 0;
}

static const char* nxi_basename(const char *path) {
    const char *last_slash = path;
    while (*path) {
        if (*path == '/') last_slash = path + 1;
        path++;
    }
    return last_slash;
}

/* ============ Custom Icons Storage ============ */

#define MAX_CUSTOM_ICONS 32

static nxicon_custom_t g_custom_icons[MAX_CUSTOM_ICONS];
static int g_custom_count = 0;
static int g_initialized = 0;

/* ============ Initialization ============ */

void nxicon_init(void) {
    if (g_initialized) return;
    
    g_custom_count = 0;
    for (int i = 0; i < MAX_CUSTOM_ICONS; i++) {
        g_custom_icons[i].folder_path[0] = '\0';
        g_custom_icons[i].icon_path[0] = '\0';
    }
    
    nxicon_load_custom();
    g_initialized = 1;
}

void nxicon_shutdown(void) {
    nxicon_save_custom();
    g_initialized = 0;
}

/* ============ Special Folder Detection ============ */

/* Special folder names (case-insensitive matching) */
static const struct {
    const char *name;
    nxfolder_special_t type;
} g_special_folders[] = {
    { "Documents",    NXFOLDER_DOCUMENTS },
    { "Downloads",    NXFOLDER_DOWNLOADS },
    { "Music",        NXFOLDER_MUSIC },
    { "Videos",       NXFOLDER_VIDEO },
    { "Movies",       NXFOLDER_VIDEO },
    { "Desktop",      NXFOLDER_DESKTOP },
    { "Library",      NXFOLDER_LIBRARY },
    { "Public",       NXFOLDER_PUBLIC },
    { "Shared",       NXFOLDER_SHARED },
    { "System",       NXFOLDER_SYSTEM },
    { "Applications", NXFOLDER_APPLICATIONS },
    { "Pictures",     NXFOLDER_CAMERA },
    { "Photos",       NXFOLDER_CAMERA },
    { "Trash",        NXFOLDER_BIN },
    { "Bin",          NXFOLDER_BIN },
    { ".Trash",       NXFOLDER_BIN },
    { NULL, NXFOLDER_GENERIC }
};

nxfolder_special_t nxicon_detect_special_folder(const char *dir_path) {
    if (!dir_path || !*dir_path) return NXFOLDER_GENERIC;
    
    const char *basename = nxi_basename(dir_path);
    
    /* Check for home folder */
    if (nxi_ends_with(dir_path, "/home") || 
        (basename[0] != '\0' && dir_path[0] == '/' && 
         nxi_strlen(dir_path) > 6 && 
         dir_path[1] == 'U' && dir_path[2] == 's' && dir_path[3] == 'e' && 
         dir_path[4] == 'r' && dir_path[5] == 's' && dir_path[6] == '/')) {
        /* User home directory - check if it's a direct child of /Users */
        int slash_count = 0;
        const char *p = dir_path;
        while (*p) {
            if (*p == '/') slash_count++;
            p++;
        }
        if (slash_count == 2) return NXFOLDER_USER;
    }
    
    /* Check special folder names */
    for (int i = 0; g_special_folders[i].name != NULL; i++) {
        if (nxi_strcmp(basename, g_special_folders[i].name) == 0) {
            return g_special_folders[i].type;
        }
    }
    
    return NXFOLDER_GENERIC;
}

/* ============ Icon Path Resolution ============ */

int nxicon_get_special_path(nxfolder_special_t type, char *out_path) {
    const char *path = NULL;
    
    switch (type) {
        case NXFOLDER_DOCUMENTS:    path = NXICON_DOCUMENTS; break;
        case NXFOLDER_DOWNLOADS:    path = NXICON_DOWNLOADS; break;
        case NXFOLDER_MUSIC:        path = NXICON_MUSIC; break;
        case NXFOLDER_VIDEO:        path = NXICON_VIDEO; break;
        case NXFOLDER_DESKTOP:      path = NXICON_DESKTOP; break;
        case NXFOLDER_LIBRARY:      path = NXICON_LIBRARY; break;
        case NXFOLDER_PUBLIC:       path = NXICON_PUBLIC; break;
        case NXFOLDER_SHARED:       path = NXICON_SHARED; break;
        case NXFOLDER_SYSTEM:       path = NXICON_SYSTEM; break;
        case NXFOLDER_USER:         path = NXICON_USER; break;
        case NXFOLDER_BIN:          path = NXICON_BIN; break;
        case NXFOLDER_CAMERA:       path = NXICON_CAMERA; break;
        case NXFOLDER_APPLICATIONS: path = NXICON_FOLDER; break;
        default: return -1;
    }
    
    if (path) {
        nxi_strcpy(out_path, path, NXICON_MAX_PATH);
        return 0;
    }
    return -1;
}

/* ============ Directory Icon Resolution ============ */

int nxicon_get_for_directory(const char *dir_path, nxicon_entry_t *out) {
    if (!out) return -1;
    
    /* Initialize output */
    out->name[0] = '\0';
    out->path[0] = '\0';
    out->source = NXICON_SOURCE_SYSTEM;
    out->type = NXICON_TYPE_DIRECTORY;
    out->fallback_color = NXICON_COLOR_FOLDER;
    out->cached = 0;
    
    /* 1. Check for user custom icon */
    char custom_path[NXICON_MAX_PATH];
    if (nxicon_get_custom(dir_path, custom_path) == 0) {
        nxi_strcpy(out->path, custom_path, NXICON_MAX_PATH);
        out->source = NXICON_SOURCE_USER;
        nxi_strcpy(out->name, "custom", NXICON_MAX_NAME);
        return 0;
    }
    
    /* 2. Check for special folder */
    nxfolder_special_t special = nxicon_detect_special_folder(dir_path);
    if (special != NXFOLDER_GENERIC) {
        if (nxicon_get_special_path(special, out->path) == 0) {
            out->type = NXICON_TYPE_SPECIAL;
            const char *name = nxi_basename(out->path);
            nxi_strcpy(out->name, name, NXICON_MAX_NAME);
            return 0;
        }
    }
    
    /* 3. Default folder icon */
    nxi_strcpy(out->path, NXICON_FOLDER, NXICON_MAX_PATH);
    nxi_strcpy(out->name, "folder", NXICON_MAX_NAME);
    return 0;
}

/* ============ File Icon Resolution ============ */

int nxicon_get_for_file(const char *filename, nxicon_entry_t *out) {
    if (!out) return -1;
    
    out->name[0] = '\0';
    out->source = NXICON_SOURCE_SYSTEM;
    out->type = NXICON_TYPE_FILE;
    out->fallback_color = NXICON_COLOR_FILE;
    out->cached = 0;
    
    /* Check extension for special icons */
    if (nxi_ends_with(filename, ".png") || nxi_ends_with(filename, ".jpg") ||
        nxi_ends_with(filename, ".jpeg") || nxi_ends_with(filename, ".gif") ||
        nxi_ends_with(filename, ".bmp") || nxi_ends_with(filename, ".webp")) {
        nxi_strcpy(out->path, NXICON_IMAGE, NXICON_MAX_PATH);
        nxi_strcpy(out->name, "image", NXICON_MAX_NAME);
        return 0;
    }
    
    /* Default file icon */
    nxi_strcpy(out->path, NXICON_FILE, NXICON_MAX_PATH);
    nxi_strcpy(out->name, "file", NXICON_MAX_NAME);
    return 0;
}

/* ============ Custom Icon Management ============ */

int nxicon_get_custom(const char *folder_path, char *out_path) {
    for (int i = 0; i < g_custom_count; i++) {
        if (nxi_strcmp(g_custom_icons[i].folder_path, folder_path) == 0) {
            nxi_strcpy(out_path, g_custom_icons[i].icon_path, NXICON_MAX_PATH);
            return 0;
        }
    }
    return -1;
}

int nxicon_set_custom(const char *folder_path, const char *icon_path) {
    /* Check if already exists */
    for (int i = 0; i < g_custom_count; i++) {
        if (nxi_strcmp(g_custom_icons[i].folder_path, folder_path) == 0) {
            nxi_strcpy(g_custom_icons[i].icon_path, icon_path, NXICON_MAX_PATH);
            nxicon_save_custom();
            return 0;
        }
    }
    
    /* Add new entry */
    if (g_custom_count < MAX_CUSTOM_ICONS) {
        nxi_strcpy(g_custom_icons[g_custom_count].folder_path, folder_path, NXICON_MAX_PATH);
        nxi_strcpy(g_custom_icons[g_custom_count].icon_path, icon_path, NXICON_MAX_PATH);
        g_custom_count++;
        nxicon_save_custom();
        return 0;
    }
    
    return -1;  /* No space */
}

int nxicon_clear_custom(const char *folder_path) {
    for (int i = 0; i < g_custom_count; i++) {
        if (nxi_strcmp(g_custom_icons[i].folder_path, folder_path) == 0) {
            /* Shift remaining entries */
            for (int j = i; j < g_custom_count - 1; j++) {
                g_custom_icons[j] = g_custom_icons[j + 1];
            }
            g_custom_count--;
            nxicon_save_custom();
            return 0;
        }
    }
    return -1;
}

/* ============ Persistence (Stub) ============ */
/* 
 * In real implementation, this would read/write to:
 * ~/.config/neolyx/folder_icons.nlc
 */

void nxicon_load_custom(void) {
    /* TODO: Load from config file */
    /* For now, no persistent custom icons */
}

void nxicon_save_custom(void) {
    /* TODO: Save to config file */
}
