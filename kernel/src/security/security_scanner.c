/*
 * NeolyxOS Security Scanner Implementation
 * 
 * File scanning, threat detection, and download warnings.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include "security/security_scanner.h"
#include "security/crypto.h"
#include "security/audit.h"

/* External dependencies */
extern void serial_puts(const char *s);
extern uint64_t pit_get_ticks(void);

/* Quarantine directory */
#define QUARANTINE_PATH "/System/Quarantine/"

/* Known threat signatures (simplified) */
#define MAX_SIGNATURES 16
typedef struct {
    uint8_t  bytes[16];
    uint8_t  len;
    char     name[32];
} threat_sig_t;

static threat_sig_t known_threats[MAX_SIGNATURES] = {
    { {0x4D, 0x5A, 0x90, 0x00, 0x03}, 5, "Suspicious DOS Header" },
    { {0x00, 0x00, 0xDE, 0xAD, 0xBE, 0xEF}, 6, "Test Malware Signature" },
};
static int sig_count = 2;

/* Scan cache */
#define MAX_SCAN_CACHE 64
static scan_info_t scan_cache[MAX_SCAN_CACHE];
static int cache_count = 0;

/* Warning callback */
static warning_callback_t warning_cb = (void *)0;

/* ============ String Helpers ============ */

static void str_copy(char *dst, const char *src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

static int str_cmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return *a - *b;
}

static int str_starts_with(const char *str, const char *prefix) {
    while (*prefix) {
        if (*str++ != *prefix++) return 0;
    }
    return 1;
}

/* ============ Initialization ============ */

void scanner_init(void) {
    serial_puts("[SCANNER] Initializing security scanner...\n");
    cache_count = 0;
    warning_cb = (void *)0;
    serial_puts("[SCANNER] Loaded ");
    /* TODO: Print signature count */
    serial_puts(" threat signatures\n");
}

/* ============ Source Detection ============ */

int scanner_detect_source(const char *path) {
    if (!path) return FILE_SOURCE_THIRDPARTY;
    
    /* System paths */
    if (str_starts_with(path, "/System/") ||
        str_starts_with(path, "/Kernel/") ||
        str_starts_with(path, "/bin/")) {
        return FILE_SOURCE_SYSTEM;
    }
    
    /* App store paths */
    if (str_starts_with(path, "/Applications/AppStore/")) {
        return FILE_SOURCE_APPSTORE;
    }
    
    /* Downloads */
    if (str_starts_with(path, "/Users/") && 
        str_starts_with(path + 7, "Downloads/")) {
        return FILE_SOURCE_DOWNLOADED;
    }
    
    /* External media */
    if (str_starts_with(path, "/Volumes/") ||
        str_starts_with(path, "/mnt/")) {
        return FILE_SOURCE_EXTERNAL;
    }
    
    return FILE_SOURCE_THIRDPARTY;
}

int scanner_is_verified(const char *path) {
    int source = scanner_detect_source(path);
    return (source == FILE_SOURCE_SYSTEM || source == FILE_SOURCE_APPSTORE);
}

/* ============ Scanning ============ */

int scanner_scan_file(const char *path) {
    if (!path) return SCAN_RESULT_ERROR;
    
    serial_puts("[SCANNER] Scanning: ");
    serial_puts(path);
    serial_puts("\n");
    
    /* Detect source */
    int source = scanner_detect_source(path);
    
    /* Check if already in cache */
    for (int i = 0; i < cache_count; i++) {
        if (str_cmp(scan_cache[i].path, path) == 0) {
            return scan_cache[i].scan_result;
        }
    }
    
    /* Create scan entry */
    scan_info_t *entry = NULL;
    if (cache_count < MAX_SCAN_CACHE) {
        entry = &scan_cache[cache_count++];
    } else {
        /* Overwrite oldest */
        entry = &scan_cache[0];
    }
    
    str_copy(entry->path, path, 256);
    entry->source_type = source;
    entry->scan_time = pit_get_ticks();
    entry->scan_result = SCAN_RESULT_CLEAN;
    entry->quarantine_status = QUARANTINE_NONE;
    entry->threat_name[0] = '\0';
    
    /* Set warning level based on source */
    switch (source) {
        case FILE_SOURCE_SYSTEM:
        case FILE_SOURCE_APPSTORE:
            entry->warning_level = WARN_LEVEL_NONE;
            break;
        case FILE_SOURCE_THIRDPARTY:
            entry->warning_level = WARN_LEVEL_CAUTION;
            scanner_show_warning(WARN_LEVEL_CAUTION, path, 
                "This file is from an unverified source");
            break;
        case FILE_SOURCE_DOWNLOADED:
            entry->warning_level = WARN_LEVEL_CAUTION;
            scanner_show_warning(WARN_LEVEL_CAUTION, path,
                "This file was downloaded from the internet");
            break;
        case FILE_SOURCE_EXTERNAL:
            entry->warning_level = WARN_LEVEL_DANGER;
            scanner_show_warning(WARN_LEVEL_DANGER, path,
                "This file is from external media - scanning required");
            break;
    }
    
    /* TODO: Actually read file and check signatures */
    /* For now, simulate clean result */
    
    /* Log scan */
    audit_log(AUDIT_TYPE_APP, AUDIT_SUCCESS, "File scanned");
    
    return entry->scan_result;
}

int scanner_scan_directory(const char *path) {
    if (!path) return SCAN_RESULT_ERROR;
    /* TODO: Iterate directory and scan each file */
    serial_puts("[SCANNER] Directory scan not implemented\n");
    return SCAN_RESULT_CLEAN;
}

int scanner_get_result(const char *path, scan_info_t *out) {
    if (!path || !out) return -1;
    
    for (int i = 0; i < cache_count; i++) {
        if (str_cmp(scan_cache[i].path, path) == 0) {
            *out = scan_cache[i];
            return 0;
        }
    }
    return -1;
}

int scanner_is_safe(const char *path) {
    int result = scanner_scan_file(path);
    return (result == SCAN_RESULT_CLEAN);
}

/* ============ Quarantine ============ */

int scanner_quarantine(const char *path) {
    if (!path) return -1;
    
    for (int i = 0; i < cache_count; i++) {
        if (str_cmp(scan_cache[i].path, path) == 0) {
            scan_cache[i].quarantine_status = QUARANTINE_ACTIVE;
            serial_puts("[SCANNER] File quarantined: ");
            serial_puts(path);
            serial_puts("\n");
            audit_log(AUDIT_TYPE_APP, AUDIT_SUCCESS, "File quarantined");
            return 0;
        }
    }
    
    /* Not in cache - add and quarantine */
    if (cache_count < MAX_SCAN_CACHE) {
        str_copy(scan_cache[cache_count].path, path, 256);
        scan_cache[cache_count].quarantine_status = QUARANTINE_ACTIVE;
        cache_count++;
        return 0;
    }
    
    return -1;
}

int scanner_release(const char *path) {
    if (!path) return -1;
    
    for (int i = 0; i < cache_count; i++) {
        if (str_cmp(scan_cache[i].path, path) == 0) {
            scan_cache[i].quarantine_status = QUARANTINE_RELEASED;
            serial_puts("[SCANNER] File released from quarantine\n");
            return 0;
        }
    }
    return -1;
}

int scanner_delete_quarantined(const char *path) {
    if (!path) return -1;
    /* TODO: Actually delete file */
    serial_puts("[SCANNER] Delete not implemented\n");
    return 0;
}

/* ============ Warnings ============ */

void scanner_set_warning_callback(warning_callback_t cb) {
    warning_cb = cb;
}

void scanner_show_warning(uint8_t level, const char *path, const char *msg) {
    /* Log to serial */
    serial_puts("[SCANNER] ");
    switch (level) {
        case WARN_LEVEL_INFO:    serial_puts("INFO: "); break;
        case WARN_LEVEL_CAUTION: serial_puts("CAUTION: "); break;
        case WARN_LEVEL_DANGER:  serial_puts("DANGER: "); break;
        default: serial_puts("WARN: "); break;
    }
    serial_puts(msg);
    serial_puts("\n");
    
    /* Call UI callback if set */
    if (warning_cb) {
        warning_cb(level, path, msg);
    }
    
    /* Log to audit */
    if (level >= WARN_LEVEL_CAUTION) {
        audit_log(AUDIT_TYPE_APP, AUDIT_FAILURE, msg);
    }
}

/* ============ Download Protection ============ */

int scanner_check_download(const char *url, const char *path) {
    if (!url || !path) return SCAN_RESULT_ERROR;
    
    serial_puts("[SCANNER] Checking download from: ");
    serial_puts(url);
    serial_puts("\n");
    
    /* Show warning for all downloads */
    scanner_show_warning(WARN_LEVEL_CAUTION, path,
        "File downloaded from internet - scanning before opening");
    
    /* Scan the file */
    return scanner_scan_file(path);
}

int scanner_verify_signature(const char *path, const uint8_t *expected) {
    if (!path || !expected) return -1;
    
    /* Compute hash */
    uint8_t computed[32];
    sha256_ctx_t ctx;
    sha256_init(&ctx);
    
    /* TODO: Read file and update hash */
    /* For now, just compare with expected */
    
    sha256_final(&ctx, computed);
    
    for (int i = 0; i < 32; i++) {
        if (computed[i] != expected[i]) {
            scanner_show_warning(WARN_LEVEL_DANGER, path,
                "Signature verification FAILED");
            return -1;
        }
    }
    
    serial_puts("[SCANNER] Signature verified\n");
    return 0;
}
