/*
 * NeolyxOS Icon Management API
 * 
 * Handles loading, caching, and custom icon support.
 * Enables per-folder icon customization like macOS/Windows.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef NXICON_H
#define NXICON_H

#include <stdint.h>

/* ============ Constants ============ */

#define NXICON_MAX_PATH     256
#define NXICON_MAX_NAME     64
#define NXICON_CACHE_SIZE   64

/* ============ Icon Source ============ */

typedef enum {
    NXICON_SOURCE_SYSTEM = 0,   /* Built-in system icon */
    NXICON_SOURCE_APP    = 1,   /* App-provided icon */
    NXICON_SOURCE_USER   = 2    /* User-customized icon */
} nxicon_source_t;

/* ============ Icon Types ============ */

typedef enum {
    NXICON_TYPE_GENERIC   = 0,  /* Generic file/folder */
    NXICON_TYPE_DIRECTORY = 1,  /* Regular folder */
    NXICON_TYPE_SPECIAL   = 2,  /* Special folder (Documents, etc.) */
    NXICON_TYPE_FILE      = 3,  /* Generic file */
    NXICON_TYPE_APP       = 4,  /* Application */
    NXICON_TYPE_SYSTEM    = 5   /* System icon */
} nxicon_type_t;

/* ============ Special Folders ============ */

typedef enum {
    NXFOLDER_GENERIC    = 0,
    NXFOLDER_DOCUMENTS  = 1,
    NXFOLDER_DOWNLOADS  = 2,
    NXFOLDER_MUSIC      = 3,
    NXFOLDER_VIDEO      = 4,
    NXFOLDER_DESKTOP    = 5,
    NXFOLDER_LIBRARY    = 6,
    NXFOLDER_PUBLIC     = 7,
    NXFOLDER_SHARED     = 8,
    NXFOLDER_SYSTEM     = 9,
    NXFOLDER_USER       = 10,
    NXFOLDER_BIN        = 11,
    NXFOLDER_CAMERA     = 12,
    NXFOLDER_APPLICATIONS = 13
} nxfolder_special_t;

/* ============ Icon Entry ============ */

typedef struct {
    char name[NXICON_MAX_NAME];
    char path[NXICON_MAX_PATH];
    nxicon_source_t source;
    nxicon_type_t type;
    uint32_t fallback_color;
    int cached;                 /* 1 if loaded in cache */
} nxicon_entry_t;

/* ============ Custom Icon Entry ============ */

typedef struct {
    char folder_path[NXICON_MAX_PATH];  /* Folder this applies to */
    char icon_path[NXICON_MAX_PATH];    /* Custom icon path */
} nxicon_custom_t;

/* ============ API Functions ============ */

/**
 * Initialize icon system
 * Call once at startup
 */
void nxicon_init(void);

/**
 * Shutdown icon system
 * Frees cache and custom icons
 */
void nxicon_shutdown(void);

/**
 * Get icon for a directory path
 * Checks: 1) user custom, 2) special folder, 3) generic
 * 
 * @param dir_path Full path to directory
 * @param out      Output icon entry
 * @return 0 on success, -1 on error
 */
int nxicon_get_for_directory(const char *dir_path, nxicon_entry_t *out);

/**
 * Get icon for a file by extension
 * 
 * @param filename Filename with extension
 * @param out      Output icon entry
 * @return 0 on success, -1 on error
 */
int nxicon_get_for_file(const char *filename, nxicon_entry_t *out);

/**
 * Detect special folder type from path
 * 
 * @param dir_path Directory path
 * @return Special folder type or NXFOLDER_GENERIC
 */
nxfolder_special_t nxicon_detect_special_folder(const char *dir_path);

/**
 * Get NXI path for special folder type
 * 
 * @param type     Special folder type
 * @param out_path Buffer for path (NXICON_MAX_PATH)
 * @return 0 on success, -1 if no special icon
 */
int nxicon_get_special_path(nxfolder_special_t type, char *out_path);

/* ============ Custom Icon API ============ */

/**
 * Set custom icon for a folder
 * Persisted to ~/.config/neolyx/folder_icons.nlc
 * 
 * @param folder_path Folder to customize
 * @param icon_path   Path to custom .nxi file
 * @return 0 on success
 */
int nxicon_set_custom(const char *folder_path, const char *icon_path);

/**
 * Clear custom icon for a folder (revert to default)
 * 
 * @param folder_path Folder to reset
 * @return 0 on success
 */
int nxicon_clear_custom(const char *folder_path);

/**
 * Get custom icon if set
 * 
 * @param folder_path Folder to check
 * @param out_path    Buffer for icon path
 * @return 0 if custom icon exists, -1 if not
 */
int nxicon_get_custom(const char *folder_path, char *out_path);

/**
 * Load custom icons from config file
 */
void nxicon_load_custom(void);

/**
 * Save custom icons to config file
 */
void nxicon_save_custom(void);

/* ============ Icon Paths ============ */

/* Base path for directory icons */
#define NXICON_DIR_BASE "/Applications/Path.app/resources/icons/dir/"

/* Built-in directory icons */
#define NXICON_FOLDER       "/Applications/Path.app/resources/icons/folder.nxi"
#define NXICON_DOCUMENTS    NXICON_DIR_BASE "documents.nxi"
#define NXICON_DOWNLOADS    NXICON_DIR_BASE "downloads.nxi"
#define NXICON_MUSIC        NXICON_DIR_BASE "music.nxi"
#define NXICON_VIDEO        NXICON_DIR_BASE "video.nxi"
#define NXICON_DESKTOP      NXICON_DIR_BASE "desktop.nxi"
#define NXICON_LIBRARY      NXICON_DIR_BASE "library.nxi"
#define NXICON_PUBLIC       NXICON_DIR_BASE "public.nxi"
#define NXICON_SHARED       NXICON_DIR_BASE "shared.nxi"
#define NXICON_SYSTEM       NXICON_DIR_BASE "system.nxi"
#define NXICON_USER         NXICON_DIR_BASE "user.nxi"
#define NXICON_BIN          NXICON_DIR_BASE "bin.nxi"
#define NXICON_CAMERA       NXICON_DIR_BASE "camera.nxi"
#define NXICON_FOLDER_ITEMS NXICON_DIR_BASE "folder_items.nxi"

/* File type icons */
#define NXICON_FILE         "/Applications/Path.app/resources/icons/file.nxi"
#define NXICON_IMAGE        "/Applications/Path.app/resources/icons/image.nxi"

/* Fallback colors */
#define NXICON_COLOR_FOLDER     0xF2D6C9
#define NXICON_COLOR_FILE       0xF5C2E7
#define NXICON_COLOR_APP        0x89B4FA

#endif /* NXICON_H */
