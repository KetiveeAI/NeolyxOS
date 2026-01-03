/*
 * NeolyxOS Security Scanner Header
 * 
 * File scanning, threat detection, and download warnings.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef NEOLYX_SECURITY_SCANNER_H
#define NEOLYX_SECURITY_SCANNER_H

#include <stdint.h>

/* Scan result codes */
#define SCAN_RESULT_CLEAN       0
#define SCAN_RESULT_SUSPICIOUS  1
#define SCAN_RESULT_THREAT      2
#define SCAN_RESULT_ERROR       -1

/* File source types */
#define FILE_SOURCE_SYSTEM      0   /* Trusted system file */
#define FILE_SOURCE_APPSTORE    1   /* Verified app store */
#define FILE_SOURCE_THIRDPARTY  2   /* Unknown source - WARNING */
#define FILE_SOURCE_DOWNLOADED  3   /* Internet download - SCAN REQUIRED */
#define FILE_SOURCE_EXTERNAL    4   /* USB/External - QUARANTINE */

/* Warning levels */
#define WARN_LEVEL_NONE         0
#define WARN_LEVEL_INFO         1
#define WARN_LEVEL_CAUTION      2
#define WARN_LEVEL_DANGER       3

/* Quarantine status */
#define QUARANTINE_NONE         0
#define QUARANTINE_PENDING      1
#define QUARANTINE_ACTIVE       2
#define QUARANTINE_RELEASED     3

/* Scan info structure */
typedef struct {
    char     path[256];
    uint32_t file_size;
    uint8_t  source_type;
    uint8_t  scan_result;
    uint8_t  warning_level;
    uint8_t  quarantine_status;
    uint64_t scan_time;
    char     threat_name[64];
} scan_info_t;

/* API Functions */
void scanner_init(void);

/* Scanning */
int  scanner_scan_file(const char *path);
int  scanner_scan_directory(const char *path);
int  scanner_get_result(const char *path, scan_info_t *out);
int  scanner_is_safe(const char *path);

/* Source detection */
int  scanner_detect_source(const char *path);
int  scanner_is_verified(const char *path);

/* Quarantine */
int  scanner_quarantine(const char *path);
int  scanner_release(const char *path);
int  scanner_delete_quarantined(const char *path);

/* Warnings */
typedef void (*warning_callback_t)(uint8_t level, const char *path, const char *msg);
void scanner_set_warning_callback(warning_callback_t cb);
void scanner_show_warning(uint8_t level, const char *path, const char *msg);

/* Download protection */
int  scanner_check_download(const char *url, const char *path);
int  scanner_verify_signature(const char *path, const uint8_t *expected);

#endif /* NEOLYX_SECURITY_SCANNER_H */
