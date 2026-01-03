/*
 * NeolyxOS NPA Package Manager Implementation
 * 
 * High-level package management operations.
 * 
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include "core/npa_manager.h"
#include "fs/npa_archive.h"
#include "fs/vfs.h"
#include "mm/kheap.h"

/* ============ External Dependencies ============ */

extern void serial_puts(const char *s);
extern void serial_putc(char c);

/* ============ String Helpers ============ */

static int npa_mgr_strlen(const char *s) {
    int len = 0;
    while (s[len]) len++;
    return len;
}

static void npa_mgr_strcpy(char *dst, const char *src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static int npa_mgr_strcmp(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return *a == *b;
}

static void npa_mgr_strcat(char *dst, const char *src, int max) {
    int len = npa_mgr_strlen(dst);
    int i = 0;
    while (src[i] && len + i < max - 1) {
        dst[len + i] = src[i];
        i++;
    }
    dst[len + i] = '\0';
}

static void npa_mgr_memset(void *s, int c, uint32_t n) {
    uint8_t *p = (uint8_t*)s;
    while (n--) *p++ = (uint8_t)c;
}

static void npa_mgr_memcpy(void *dst, const void *src, uint32_t n) {
    uint8_t *d = (uint8_t*)dst;
    const uint8_t *s = (const uint8_t*)src;
    while (n--) *d++ = *s++;
}

/* ============ Package Database ============ */

/* Simple in-memory database (persists to disk on sync) */
static npa_package_info_t installed_packages[NPA_MAX_PACKAGES];
static int installed_count = 0;
static int database_loaded = 0;

/* ============ Database Operations ============ */

static int db_find_package(const char *name) {
    for (int i = 0; i < installed_count; i++) {
        if (npa_mgr_strcmp(installed_packages[i].name, name)) {
            return i;
        }
    }
    return -1;
}

static int db_add_package(const npa_package_info_t *pkg) {
    if (installed_count >= NPA_MAX_PACKAGES) {
        return -1;
    }
    
    /* Check for duplicates */
    int existing = db_find_package(pkg->name);
    if (existing >= 0) {
        /* Update existing entry */
        npa_mgr_memcpy(&installed_packages[existing], pkg, sizeof(npa_package_info_t));
        return 0;
    }
    
    /* Add new entry */
    npa_mgr_memcpy(&installed_packages[installed_count], pkg, sizeof(npa_package_info_t));
    installed_count++;
    
    return 0;
}

static int db_remove_package(const char *name) {
    int index = db_find_package(name);
    if (index < 0) {
        return -1;
    }
    
    /* Shift remaining entries */
    for (int i = index; i < installed_count - 1; i++) {
        npa_mgr_memcpy(&installed_packages[i], &installed_packages[i + 1], 
                       sizeof(npa_package_info_t));
    }
    
    installed_count--;
    return 0;
}

/* ============ Manager Initialization ============ */

int npa_manager_init(void) {
    serial_puts("[NPA Manager] Initializing\n");
    
    /* Clear database */
    npa_mgr_memset(installed_packages, 0, sizeof(installed_packages));
    installed_count = 0;
    
    /* Ensure packages directory exists */
    vfs_create(NPA_PACKAGES_DIR, VFS_DIRECTORY, 0755);
    vfs_create("/apps", VFS_DIRECTORY, 0755);
    
    /* Try to load existing database */
    vfs_file_t *db_file = vfs_open(NPA_DATABASE_PATH, VFS_O_RDONLY);
    if (db_file) {
        /* Read package count */
        int32_t count = 0;
        vfs_read(db_file, &count, sizeof(count));
        
        if (count > 0 && count <= NPA_MAX_PACKAGES) {
            vfs_read(db_file, installed_packages, 
                     count * sizeof(npa_package_info_t));
            installed_count = count;
            
            serial_puts("[NPA Manager] Loaded ");
            char num[16];
            int n = count, idx = 0;
            if (n == 0) num[idx++] = '0';
            else {
                char tmp[16];
                int j = 0;
                while (n > 0) { tmp[j++] = '0' + (n % 10); n /= 10; }
                while (j > 0) num[idx++] = tmp[--j];
            }
            num[idx] = '\0';
            serial_puts(num);
            serial_puts(" packages from database\n");
        }
        
        vfs_close(db_file);
    }
    
    database_loaded = 1;
    serial_puts("[NPA Manager] Ready\n");
    return 0;
}

int npa_manager_sync(void) {
    serial_puts("[NPA Manager] Syncing database to disk\n");
    
    /* Create/update database file */
    vfs_create(NPA_DATABASE_PATH, VFS_FILE, 0644);
    
    vfs_file_t *db_file = vfs_open(NPA_DATABASE_PATH, VFS_O_WRONLY);
    if (!db_file) {
        serial_puts("[NPA Manager] Failed to open database for writing\n");
        return -1;
    }
    
    /* Write package count */
    int32_t count = installed_count;
    vfs_write(db_file, &count, sizeof(count));
    
    /* Write package entries */
    if (installed_count > 0) {
        vfs_write(db_file, installed_packages, 
                  installed_count * sizeof(npa_package_info_t));
    }
    
    vfs_close(db_file);
    
    serial_puts("[NPA Manager] Database synced\n");
    return 0;
}

/* ============ Package Operations ============ */

int npa_install(const char *npa_path) {
    if (!npa_path) {
        return -1;
    }
    
    serial_puts("[NPA Manager] Installing from: ");
    serial_puts(npa_path);
    serial_puts("\n");
    
    /* Initialize manager if needed */
    if (!database_loaded) {
        npa_manager_init();
    }
    
    /* Open the NPA archive */
    npa_archive_t archive;
    int result = npa_open(npa_path, &archive);
    if (result != 0) {
        serial_puts("[NPA Manager] Failed to open archive\n");
        return -2;
    }
    
    /* Verify archive integrity */
    result = npa_archive_verify(&archive);
    if (result != 0) {
        serial_puts("[NPA Manager] Archive verification failed\n");
        npa_close(&archive);
        return -3;
    }
    
    /* Get manifest */
    const npa_manifest_t *manifest = npa_get_manifest(&archive);
    if (!manifest || manifest->name[0] == '\0') {
        serial_puts("[NPA Manager] Invalid manifest\n");
        npa_close(&archive);
        return -4;
    }
    
    /* Check if already installed */
    int existing = db_find_package(manifest->name);
    if (existing >= 0) {
        serial_puts("[NPA Manager] Package already installed: ");
        serial_puts(manifest->name);
        serial_puts("\n");
        serial_puts("[NPA Manager] Use 'npa update' to upgrade\n");
        npa_close(&archive);
        return -5;
    }
    
    /* Build install path */
    char install_path[256];
    if (manifest->install_path[0] != '\0') {
        npa_mgr_strcpy(install_path, manifest->install_path, 256);
    } else {
        npa_mgr_strcpy(install_path, NPA_PACKAGES_DIR, 256);
        npa_mgr_strcat(install_path, "/", 256);
        npa_mgr_strcat(install_path, manifest->name, 256);
    }
    
    serial_puts("[NPA Manager] Installing to: ");
    serial_puts(install_path);
    serial_puts("\n");
    
    /* Extract all files */
    int extracted = npa_extract_all(&archive, install_path);
    if (extracted < 0) {
        serial_puts("[NPA Manager] Extraction failed\n");
        npa_close(&archive);
        return -6;
    }
    
    /* Create package info entry */
    npa_package_info_t pkg_info;
    npa_mgr_memset(&pkg_info, 0, sizeof(pkg_info));
    
    npa_mgr_strcpy(pkg_info.name, manifest->name, 64);
    npa_mgr_strcpy(pkg_info.version, manifest->version, 32);
    npa_mgr_strcpy(pkg_info.description, manifest->description, 256);
    npa_mgr_strcpy(pkg_info.author, manifest->author, 64);
    npa_mgr_strcpy(pkg_info.install_path, install_path, 128);
    pkg_info.file_count = extracted;
    pkg_info.status = NPA_STATUS_INSTALLED;
    
    /* Get current time (use timer ticks as timestamp) */
    extern uint32_t timer_get_ticks(void);
    pkg_info.installed_time = timer_get_ticks();
    
    /* Add to database */
    db_add_package(&pkg_info);
    
    /* Sync to disk */
    npa_manager_sync();
    
    npa_close(&archive);
    
    serial_puts("[NPA Manager] Package installed successfully: ");
    serial_puts(manifest->name);
    serial_puts("\n");
    
    return 0;
}

int npa_remove(const char *package_name) {
    if (!package_name) {
        return -1;
    }
    
    serial_puts("[NPA Manager] Removing package: ");
    serial_puts(package_name);
    serial_puts("\n");
    
    /* Initialize manager if needed */
    if (!database_loaded) {
        npa_manager_init();
    }
    
    /* Find package in database */
    int index = db_find_package(package_name);
    if (index < 0) {
        serial_puts("[NPA Manager] Package not found\n");
        return -2;
    }
    
    npa_package_info_t *pkg = &installed_packages[index];
    
    /* Delete installed directory */
    /* Note: VFS unlink for directories would need recursive delete */
    /* For now, we just remove the database entry */
    serial_puts("[NPA Manager] Removing from: ");
    serial_puts(pkg->install_path);
    serial_puts("\n");
    
    /* Attempt to remove install directory */
    vfs_unlink(pkg->install_path);
    
    /* Remove from database */
    db_remove_package(package_name);
    
    /* Sync to disk */
    npa_manager_sync();
    
    serial_puts("[NPA Manager] Package removed\n");
    return 0;
}

int npa_list(npa_package_info_t *packages, int max, int *count) {
    if (!packages || !count || max <= 0) {
        return -1;
    }
    
    /* Initialize manager if needed */
    if (!database_loaded) {
        npa_manager_init();
    }
    
    int to_copy = installed_count < max ? installed_count : max;
    
    for (int i = 0; i < to_copy; i++) {
        npa_mgr_memcpy(&packages[i], &installed_packages[i], 
                       sizeof(npa_package_info_t));
    }
    
    *count = to_copy;
    return 0;
}

int npa_info(const char *package_name, npa_package_info_t *info) {
    if (!package_name || !info) {
        return -1;
    }
    
    /* Initialize manager if needed */
    if (!database_loaded) {
        npa_manager_init();
    }
    
    int index = db_find_package(package_name);
    if (index < 0) {
        return -1;
    }
    
    npa_mgr_memcpy(info, &installed_packages[index], sizeof(npa_package_info_t));
    return 0;
}

int npa_search(const char *query, npa_package_info_t *results, int max, int *count) {
    if (!query || !results || !count || max <= 0) {
        return -1;
    }
    
    /* Initialize manager if needed */
    if (!database_loaded) {
        npa_manager_init();
    }
    
    int found = 0;
    
    /* Simple substring search in name and description */
    for (int i = 0; i < installed_count && found < max; i++) {
        /* Check if query is in name */
        int match = 0;
        const char *haystack = installed_packages[i].name;
        int haylen = npa_mgr_strlen(haystack);
        int querylen = npa_mgr_strlen(query);
        
        for (int j = 0; j <= haylen - querylen; j++) {
            int m = 1;
            for (int k = 0; k < querylen; k++) {
                if (haystack[j + k] != query[k]) { m = 0; break; }
            }
            if (m) { match = 1; break; }
        }
        
        if (match) {
            npa_mgr_memcpy(&results[found], &installed_packages[i], 
                          sizeof(npa_package_info_t));
            found++;
        }
    }
    
    *count = found;
    return 0;
}

int npa_verify(const char *package_name) {
    if (!package_name) {
        return -1;
    }
    
    /* Initialize manager if needed */
    if (!database_loaded) {
        npa_manager_init();
    }
    
    int index = db_find_package(package_name);
    if (index < 0) {
        return -1;
    }
    
    npa_package_info_t *pkg = &installed_packages[index];
    
    serial_puts("[NPA Manager] Verifying package: ");
    serial_puts(package_name);
    serial_puts("\n");
    
    /* Check if install directory exists */
    vfs_file_t *dir = vfs_open(pkg->install_path, VFS_O_RDONLY);
    if (!dir) {
        serial_puts("[NPA Manager] Install directory missing\n");
        pkg->status = NPA_STATUS_BROKEN;
        return -2;
    }
    vfs_close(dir);
    
    serial_puts("[NPA Manager] Package verified OK\n");
    return 0;
}

/* ============ Update System ============ */

int npa_check_updates(void) {
    /* Initialize manager if needed */
    if (!database_loaded) {
        npa_manager_init();
    }
    
    serial_puts("[NPA Manager] Checking for updates in ");
    serial_puts(NPA_UPDATES_DIR);
    serial_puts("\n");
    
    /* List .npa files in updates directory */
    vfs_dirent_t entries[32];
    uint32_t count = 0;
    
    int result = vfs_readdir(NPA_UPDATES_DIR, entries, 32, &count);
    if (result != 0) {
        serial_puts("[NPA Manager] No updates directory found\n");
        return 0;
    }
    
    int updates_found = 0;
    
    for (uint32_t i = 0; i < count; i++) {
        /* Check for .npa extension */
        int len = npa_mgr_strlen(entries[i].name);
        if (len > 4) {
            const char *ext = entries[i].name + len - 4;
            if (ext[0] == '.' && ext[1] == 'n' && ext[2] == 'p' && ext[3] == 'a') {
                updates_found++;
                serial_puts("[NPA Manager] Update found: ");
                serial_puts(entries[i].name);
                serial_puts("\n");
            }
        }
    }
    
    return updates_found;
}

int npa_apply_update(const char *npa_path) {
    if (!npa_path) {
        return -1;
    }
    
    /* Open and check version */
    npa_archive_t archive;
    int result = npa_open(npa_path, &archive);
    if (result != 0) {
        return -2;
    }
    
    const npa_manifest_t *manifest = npa_get_manifest(&archive);
    if (!manifest) {
        npa_close(&archive);
        return -3;
    }
    
    /* Check if installed */
    int index = db_find_package(manifest->name);
    if (index >= 0) {
        /* Compare versions */
        int cmp = npa_compare_versions(manifest->version, 
                                       installed_packages[index].version);
        if (cmp <= 0) {
            serial_puts("[NPA Manager] Already up to date: ");
            serial_puts(manifest->name);
            serial_puts("\n");
            npa_close(&archive);
            return 0;
        }
        
        /* Remove old version first */
        npa_remove(manifest->name);
    }
    
    npa_close(&archive);
    
    /* Install new version */
    return npa_install(npa_path);
}

int npa_apply_all_updates(void) {
    serial_puts("[NPA Manager] Applying all updates\n");
    
    vfs_dirent_t entries[32];
    uint32_t count = 0;
    
    int result = vfs_readdir(NPA_UPDATES_DIR, entries, 32, &count);
    if (result != 0) {
        return 0;
    }
    
    int applied = 0;
    
    for (uint32_t i = 0; i < count; i++) {
        int len = npa_mgr_strlen(entries[i].name);
        if (len > 4) {
            const char *ext = entries[i].name + len - 4;
            if (ext[0] == '.' && ext[1] == 'n' && ext[2] == 'p' && ext[3] == 'a') {
                char full_path[256];
                npa_mgr_strcpy(full_path, NPA_UPDATES_DIR, 256);
                npa_mgr_strcat(full_path, "/", 256);
                npa_mgr_strcat(full_path, entries[i].name, 256);
                
                if (npa_apply_update(full_path) == 0) {
                    applied++;
                }
            }
        }
    }
    
    return applied;
}

/* ============ Utility Functions ============ */

int npa_is_installed(const char *package_name) {
    if (!database_loaded) {
        npa_manager_init();
    }
    return db_find_package(package_name) >= 0;
}

int npa_get_version(const char *package_name, char *version, int max) {
    if (!package_name || !version || max <= 0) {
        return -1;
    }
    
    if (!database_loaded) {
        npa_manager_init();
    }
    
    int index = db_find_package(package_name);
    if (index < 0) {
        return -1;
    }
    
    npa_mgr_strcpy(version, installed_packages[index].version, max);
    return 0;
}

int npa_compare_versions(const char *v1, const char *v2) {
    /* Simple version comparison: major.minor.patch */
    int m1 = 0, n1 = 0, p1 = 0;
    int m2 = 0, n2 = 0, p2 = 0;
    
    /* Parse v1 */
    const char *s = v1;
    while (*s && *s != '.') { m1 = m1 * 10 + (*s - '0'); s++; }
    if (*s == '.') s++;
    while (*s && *s != '.') { n1 = n1 * 10 + (*s - '0'); s++; }
    if (*s == '.') s++;
    while (*s && *s >= '0' && *s <= '9') { p1 = p1 * 10 + (*s - '0'); s++; }
    
    /* Parse v2 */
    s = v2;
    while (*s && *s != '.') { m2 = m2 * 10 + (*s - '0'); s++; }
    if (*s == '.') s++;
    while (*s && *s != '.') { n2 = n2 * 10 + (*s - '0'); s++; }
    if (*s == '.') s++;
    while (*s && *s >= '0' && *s <= '9') { p2 = p2 * 10 + (*s - '0'); s++; }
    
    /* Compare */
    if (m1 != m2) return m1 < m2 ? -1 : 1;
    if (n1 != n2) return n1 < n2 ? -1 : 1;
    if (p1 != p2) return p1 < p2 ? -1 : 1;
    
    return 0;
}
