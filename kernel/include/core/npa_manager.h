/*
 * NeolyxOS NPA Package Manager
 * 
 * High-level package management: install, remove, list, search, verify.
 * Works with NXFS filesystem for package storage.
 * 
 * Package Database Location: /System/packages.db
 * Package Install Path:      /apps/packages/<name>/
 * Update Source:             /System/updates/
 * 
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#ifndef NEOLYX_NPA_MANAGER_H
#define NEOLYX_NPA_MANAGER_H

#include <stdint.h>

/* ============ Package Paths ============ */

#define NPA_PACKAGES_DIR    "/apps/packages"
#define NPA_UPDATES_DIR     "/System/updates"
#define NPA_DATABASE_PATH   "/System/packages.db"
#define NPA_MAX_PACKAGES    256

/* ============ Package Status ============ */

typedef enum {
    NPA_STATUS_NOT_INSTALLED = 0,
    NPA_STATUS_INSTALLED,
    NPA_STATUS_BROKEN,          /* Files missing or corrupted */
    NPA_STATUS_UPDATE_AVAILABLE
} npa_status_t;

/* ============ Package Info Structure ============ */

typedef struct {
    char        name[64];         /* Package identifier */
    char        version[32];      /* Installed version */
    char        description[256]; /* Description */
    char        author[64];       /* Author/maintainer */
    char        install_path[128];/* Where installed */
    uint32_t    size;             /* Installed size in bytes */
    uint32_t    file_count;       /* Number of installed files */
    uint32_t    installed_time;   /* Unix timestamp of install */
    npa_status_t status;          /* Current status */
} npa_package_info_t;

/* ============ Manager Initialization ============ */

/**
 * npa_manager_init - Initialize package manager
 * Loads package database from disk.
 * @return: 0 on success, negative on error
 */
int npa_manager_init(void);

/**
 * npa_manager_sync - Sync package database to disk
 * @return: 0 on success, negative on error
 */
int npa_manager_sync(void);

/* ============ Package Operations ============ */

/**
 * npa_install - Install package from .npa file
 * @npa_path: Path to .npa archive
 * @return: 0 on success, negative on error
 * 
 * Steps:
 * 1. Open and verify archive
 * 2. Check if already installed (prevent duplicates)
 * 3. Extract to /apps/packages/<name>/
 * 4. Run post-install script if present
 * 5. Register in database
 */
int npa_install(const char *npa_path);

/**
 * npa_remove - Remove installed package
 * @package_name: Name of package to remove
 * @return: 0 on success, negative on error
 * 
 * Steps:
 * 1. Look up in database
 * 2. Run pre-remove script if present
 * 3. Delete all installed files
 * 4. Remove from database
 */
int npa_remove(const char *package_name);

/**
 * npa_list - List all installed packages
 * @packages: Output array
 * @max: Maximum entries to return
 * @count: Output count of packages
 * @return: 0 on success, negative on error
 */
int npa_list(npa_package_info_t *packages, int max, int *count);

/**
 * npa_info - Get info about specific package
 * @package_name: Package to query
 * @info: Output info structure
 * @return: 0 on success, -1 if not found
 */
int npa_info(const char *package_name, npa_package_info_t *info);

/**
 * npa_search - Search packages by name/description
 * @query: Search string
 * @results: Output array
 * @max: Maximum results
 * @count: Output result count
 * @return: 0 on success
 */
int npa_search(const char *query, npa_package_info_t *results, int max, int *count);

/**
 * npa_verify - Verify installed package integrity
 * @package_name: Package to verify
 * @return: 0 if valid, negative if corrupted
 */
int npa_verify(const char *package_name);

/* ============ Update System ============ */

/**
 * npa_check_updates - Check for available updates
 * Scans /System/updates/ for .npa files newer than installed versions.
 * @return: Number of updates available
 */
int npa_check_updates(void);

/**
 * npa_apply_update - Apply single update
 * @npa_path: Path to update .npa file
 * @return: 0 on success
 */
int npa_apply_update(const char *npa_path);

/**
 * npa_apply_all_updates - Apply all pending updates
 * @return: Number of updates applied, negative on error
 */
int npa_apply_all_updates(void);

/* ============ Utility Functions ============ */

/**
 * npa_is_installed - Check if package is installed
 * @package_name: Package name
 * @return: 1 if installed, 0 if not
 */
int npa_is_installed(const char *package_name);

/**
 * npa_get_version - Get installed version of package
 * @package_name: Package name
 * @version: Output buffer for version string
 * @max: Buffer size
 * @return: 0 on success, -1 if not installed
 */
int npa_get_version(const char *package_name, char *version, int max);

/**
 * npa_compare_versions - Compare two version strings
 * @v1: First version
 * @v2: Second version
 * @return: -1 if v1<v2, 0 if equal, 1 if v1>v2
 */
int npa_compare_versions(const char *v1, const char *v2);

#endif /* NEOLYX_NPA_MANAGER_H */
