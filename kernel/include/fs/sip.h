/*
 * NeolyxOS System Integrity Protection (SIP)
 * 
 * Protects critical system files from modification,
 * similar to macOS System Integrity Protection.
 * 
 * Protected paths (read-only for all non-recovery processes):
 *   /System/    - Core system files
 *   /Kernel/    - Kernel and drivers
 *   /Library/   - System libraries
 *   /Boot/      - Bootloader files
 * 
 * User-writable paths:
 *   /Users/     - User data
 *   /Applications/ - User apps (admin to install)
 *   /tmp/       - Temporary files
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef NEOLYX_SIP_H
#define NEOLYX_SIP_H

#include <stdint.h>

/* ============ SIP Status ============ */

typedef enum {
    SIP_ENABLED  = 1,   /* Full protection */
    SIP_DISABLED = 0,   /* No protection (recovery mode only) */
    SIP_PERMISSIVE = 2, /* Log violations but allow (debugging) */
} sip_status_t;

/* ============ Protection Levels ============ */

typedef enum {
    SIP_PROT_NONE       = 0x00,  /* No protection */
    SIP_PROT_READ_ONLY  = 0x01,  /* Can't write or delete */
    SIP_PROT_NO_DELETE  = 0x02,  /* Can write but not delete */
    SIP_PROT_SYSTEM     = 0x04,  /* Only system processes can modify */
    SIP_PROT_ADMIN      = 0x08,  /* Requires admin privilege */
    SIP_PROT_IMMUTABLE  = 0x10,  /* Can never be modified (even recovery) */
} sip_protection_t;

/* ============ Path Rule ============ */

typedef struct {
    const char *path;           /* Path prefix to match */
    uint8_t protection;         /* Protection flags */
    uint8_t recursive;          /* Apply to subdirectories */
} sip_rule_t;

/* ============ Violation Info ============ */

typedef struct {
    const char *path;           /* Path that was accessed */
    const char *operation;      /* "write", "delete", "mkdir", etc. */
    int32_t pid;                /* Process that attempted access */
    uint32_t protection;        /* Protection level that was violated */
    uint64_t timestamp;         /* When violation occurred */
} sip_violation_t;

/* ============ SIP API ============ */

/**
 * sip_init - Initialize System Integrity Protection
 * 
 * Loads protection rules and enables SIP.
 * 
 * Returns: 0 on success, -1 on error
 */
int sip_init(void);

/**
 * sip_is_enabled - Check if SIP is enabled
 * 
 * Returns: SIP status
 */
sip_status_t sip_is_enabled(void);

/**
 * sip_enable - Enable SIP
 * 
 * Only callable in recovery mode.
 * 
 * Returns: 0 on success, -1 on error
 */
int sip_enable(void);

/**
 * sip_disable - Disable SIP
 * 
 * Only callable in recovery mode.
 * 
 * Returns: 0 on success, -1 on error
 */
int sip_disable(void);

/**
 * sip_check_path - Check if a path operation is allowed
 * 
 * @path: Full path to check
 * @operation: Operation type (SIP_OP_WRITE, etc.)
 * 
 * Returns: 0 if allowed, -EACCES if denied
 */
int sip_check_path(const char *path, int operation);

/* Operation types */
#define SIP_OP_READ     0
#define SIP_OP_WRITE    1
#define SIP_OP_DELETE   2
#define SIP_OP_CREATE   3
#define SIP_OP_MKDIR    4
#define SIP_OP_RENAME   5
#define SIP_OP_CHMOD    6
#define SIP_OP_CHOWN    7

/**
 * sip_check_writable - Quick check if path is writable
 * 
 * @path: Path to check
 * 
 * Returns: 1 if writable, 0 if protected
 */
int sip_check_writable(const char *path);

/**
 * sip_get_protection - Get protection level for path
 * 
 * @path: Path to check
 * 
 * Returns: Protection flags
 */
uint8_t sip_get_protection(const char *path);

/**
 * sip_add_rule - Add a protection rule
 * 
 * @rule: Rule to add
 * 
 * Returns: 0 on success, -1 on error
 */
int sip_add_rule(const sip_rule_t *rule);

/**
 * sip_log_violation - Log a SIP violation
 * 
 * @violation: Violation details
 */
void sip_log_violation(const sip_violation_t *violation);

/**
 * sip_get_violation_count - Get number of logged violations
 * 
 * Returns: Count of violations
 */
uint32_t sip_get_violation_count(void);

/**
 * sip_print_status - Print SIP status to serial
 */
void sip_print_status(void);

#endif /* NEOLYX_SIP_H */
