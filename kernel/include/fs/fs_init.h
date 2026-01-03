/*
 * NeolyxOS Filesystem Initialization Header
 * 
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#ifndef FS_INIT_H
#define FS_INIT_H

#include <stdint.h>

/* ============ Standard User Directories ============ */

typedef enum {
    FS_DIR_HOME = 0,
    FS_DIR_DESKTOP,
    FS_DIR_DOCUMENTS,
    FS_DIR_DOWNLOADS,
    FS_DIR_PICTURES,
    FS_DIR_MUSIC,
    FS_DIR_VIDEOS,
    FS_DIR_PUBLIC,
    FS_DIR_APPS,
    FS_DIR_LIBRARY,
    FS_DIR_CONFIG
} fs_user_dir_t;

/* ============ Filesystem Paths - FROZEN ABI v1.0 ============ */

/* Core System */
#define FS_PATH_SYSTEM       "/System"
#define FS_PATH_KERNEL       "/System/Kernel"
#define FS_PATH_DRIVERS      "/System/Drivers"

/* System-wide */
#define FS_PATH_APPLICATIONS "/Applications"
#define FS_PATH_LIBRARY      "/Library"
#define FS_PATH_FRAMEWORKS   "/Library/Frameworks"
#define FS_PATH_BIN          "/Library/Bin"

/* Audio Plugins */
#define FS_PATH_AUDIO        "/Library/Audio"
#define FS_PATH_VST          "/Library/Audio/Plugins/VST"
#define FS_PATH_VST3         "/Library/Audio/Plugins/VST3"
#define FS_PATH_AU           "/Library/Audio/Plugins/AU"
#define FS_PATH_CLAP         "/Library/Audio/Plugins/CLAP"
#define FS_PATH_PRESETS      "/Library/Audio/Presets"

/* Language Runtimes */
#define FS_PATH_RUNTIMES     "/Library/Runtimes"
#define FS_PATH_PYTHON       "/Library/Runtimes/Python"
#define FS_PATH_NODE         "/Library/Runtimes/Node"
#define FS_PATH_RUST         "/Library/Runtimes/Rust"
#define FS_PATH_GO           "/Library/Runtimes/Go"
#define FS_PATH_JAVA         "/Library/Runtimes/Java"

/* Users */
#define FS_PATH_USERS        "/Users"
#define FS_PATH_SHARED       "/Users/Shared"

/* Public Storage */
#define FS_PATH_PUBLIC       "/Public"
#define FS_PATH_PUBLIC_DOCS  "/Public/Documents"
#define FS_PATH_PUBLIC_PICS  "/Public/Pictures"
#define FS_PATH_PUBLIC_MUSIC "/Public/Music"
#define FS_PATH_PUBLIC_VIDS  "/Public/Videos"

/* Developer */
#define FS_PATH_DEVELOPER    "/Developer"
#define FS_PATH_SDKS         "/Developer/SDKs"
#define FS_PATH_TOOLCHAINS   "/Developer/Toolchains"

/* Games (First-Class) */
#define FS_PATH_GAMES        "/Games"

/* Caches (Volatile) */
#define FS_PATH_CACHES       "/Caches"
#define FS_PATH_SHADER_CACHE "/Caches/ShaderCache"

/* Crash Reports */
#define FS_PATH_CRASHREPORTS "/CrashReports"

/* Storage */
#define FS_PATH_VOLUMES      "/Volumes"
#define FS_PATH_BOOT         "/Boot"
#define FS_PATH_TMP          "/tmp"

/* ============ API Functions ============ */

/**
 * Initialize the entire filesystem structure.
 * Creates root directories and default users.
 * Call once during kernel boot.
 */
int fs_init(void);

/**
 * Create root directory structure.
 * Called by fs_init().
 */
int fs_init_root_structure(void);

/**
 * Create home directory for a user.
 * Creates /Users/{username}/ with all subdirectories.
 *
 * @param username  The username (e.g., "alice")
 * @return 0 on success, -1 on failure
 */
int fs_create_user_home(const char *username);

/**
 * Get path to a standard user directory.
 *
 * @param username  The username
 * @param dir       Directory type (FS_DIR_DESKTOP, etc.)
 * @param path      Output buffer for path
 * @param max_len   Buffer size
 * @return 0 on success
 */
int fs_get_user_dir(const char *username, fs_user_dir_t dir, char *path, int max_len);

#endif /* FS_INIT_H */
