/*
 * NeolyxOS System Integrity Protection (SIP) Implementation
 * 
 * Protects critical system files from modification.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include <stdint.h>
#include "../../include/fs/sip.h"

/* ============ External Dependencies ============ */

extern void serial_puts(const char *s);
extern void serial_putc(char c);
extern uint64_t pit_get_ticks(void);
extern int get_current_pid(void);

/* Boot guard for recovery mode check */
extern int boot_guard_get_mode(void);
#define BOOT_MODE_RECOVERY 0x02

/* ============ Static State ============ */

static sip_status_t g_sip_status = SIP_ENABLED;
static uint32_t g_violation_count = 0;

/* Maximum rules */
#define SIP_MAX_RULES 32
static sip_rule_t g_rules[SIP_MAX_RULES];
static int g_rule_count = 0;

/* ============ Default Protected Paths ============ */

/* System directories - cannot be modified */
static const sip_rule_t g_default_rules[] = {
    /* Core system - fully protected */
    { "/System/",   SIP_PROT_READ_ONLY | SIP_PROT_NO_DELETE, 1 },
    { "/Kernel/",   SIP_PROT_READ_ONLY | SIP_PROT_NO_DELETE, 1 },
    { "/Boot/",     SIP_PROT_READ_ONLY | SIP_PROT_NO_DELETE, 1 },
    
    /* Libraries - protected, admin can update */
    { "/Library/",  SIP_PROT_SYSTEM | SIP_PROT_ADMIN, 1 },
    
    /* Core binaries */
    { "/bin/",      SIP_PROT_READ_ONLY, 1 },
    { "/sbin/",     SIP_PROT_READ_ONLY | SIP_PROT_SYSTEM, 1 },
    
    /* Applications - admin can install/remove */
    { "/Applications/", SIP_PROT_ADMIN, 1 },
    
    /* User directories - no protection */
    { "/Users/",    SIP_PROT_NONE, 1 },
    { "/tmp/",      SIP_PROT_NONE, 1 },
    { "/var/tmp/",  SIP_PROT_NONE, 1 },
    { "/home/",     SIP_PROT_NONE, 1 },
    
    /* Sentinel */
    { NULL, 0, 0 }
};

/* ============ Utility Functions ============ */

static void serial_dec(int64_t val) {
    if (val < 0) { serial_putc('-'); val = -val; }
    char buf[21];
    int i = 20;
    buf[i--] = '\0';
    if (val == 0) { serial_putc('0'); return; }
    while (val > 0) { buf[i--] = '0' + (val % 10); val /= 10; }
    serial_puts(&buf[i + 1]);
}

/* Check if path starts with prefix */
static int path_starts_with(const char *path, const char *prefix) {
    if (!path || !prefix) return 0;
    
    while (*prefix) {
        if (*path != *prefix) return 0;
        path++;
        prefix++;
    }
    return 1;
}

/* Get string length */
static int sip_strlen(const char *s) {
    int len = 0;
    while (s && s[len]) len++;
    return len;
}

/* ============ SIP Core Functions ============ */

int sip_init(void) {
    serial_puts("[SIP] Initializing System Integrity Protection...\n");
    
    /* Check if in recovery mode */
    if (boot_guard_get_mode() & BOOT_MODE_RECOVERY) {
        serial_puts("[SIP] Recovery mode detected - SIP disabled\n");
        g_sip_status = SIP_DISABLED;
    } else {
        g_sip_status = SIP_ENABLED;
    }
    
    /* Load default rules */
    g_rule_count = 0;
    for (int i = 0; g_default_rules[i].path != NULL && i < SIP_MAX_RULES; i++) {
        g_rules[g_rule_count++] = g_default_rules[i];
    }
    
    serial_puts("[SIP] Loaded ");
    serial_dec(g_rule_count);
    serial_puts(" protection rules\n");
    
    sip_print_status();
    
    return 0;
}

sip_status_t sip_is_enabled(void) {
    return g_sip_status;
}

int sip_enable(void) {
    /* Only allow in recovery mode */
    if (!(boot_guard_get_mode() & BOOT_MODE_RECOVERY)) {
        serial_puts("[SIP] Error: Can only enable SIP in recovery mode\n");
        return -1;
    }
    
    g_sip_status = SIP_ENABLED;
    serial_puts("[SIP] System Integrity Protection ENABLED\n");
    
    return 0;
}

int sip_disable(void) {
    /* Only allow in recovery mode */
    if (!(boot_guard_get_mode() & BOOT_MODE_RECOVERY)) {
        serial_puts("[SIP] Error: Can only disable SIP in recovery mode\n");
        return -1;
    }
    
    g_sip_status = SIP_DISABLED;
    serial_puts("[SIP] System Integrity Protection DISABLED\n");
    serial_puts("[SIP] WARNING: System files are now unprotected!\n");
    
    return 0;
}

uint8_t sip_get_protection(const char *path) {
    if (!path) return SIP_PROT_NONE;
    
    /* Find matching rule (longest prefix wins) */
    const sip_rule_t *best_match = NULL;
    int best_len = 0;
    
    for (int i = 0; i < g_rule_count; i++) {
        if (path_starts_with(path, g_rules[i].path)) {
            int len = sip_strlen(g_rules[i].path);
            if (len > best_len) {
                best_len = len;
                best_match = &g_rules[i];
            }
        }
    }
    
    if (best_match) {
        return best_match->protection;
    }
    
    /* Default: no protection for unlisted paths */
    return SIP_PROT_NONE;
}

int sip_check_path(const char *path, int operation) {
    /* SIP disabled - allow everything */
    if (g_sip_status == SIP_DISABLED) {
        return 0;
    }
    
    /* Read operations always allowed */
    if (operation == SIP_OP_READ) {
        return 0;
    }
    
    /* Get protection level */
    uint8_t prot = sip_get_protection(path);
    
    /* No protection - allow */
    if (prot == SIP_PROT_NONE) {
        return 0;
    }
    
    /* Check specific protections */
    int denied = 0;
    
    switch (operation) {
        case SIP_OP_WRITE:
        case SIP_OP_CREATE:
        case SIP_OP_MKDIR:
        case SIP_OP_CHMOD:
        case SIP_OP_CHOWN:
            if (prot & SIP_PROT_READ_ONLY) denied = 1;
            if (prot & SIP_PROT_SYSTEM) denied = 1;  /* TODO: Check if system process */
            break;
            
        case SIP_OP_DELETE:
        case SIP_OP_RENAME:
            if (prot & SIP_PROT_READ_ONLY) denied = 1;
            if (prot & SIP_PROT_NO_DELETE) denied = 1;
            if (prot & SIP_PROT_SYSTEM) denied = 1;
            break;
    }
    
    /* Admin check */
    if ((prot & SIP_PROT_ADMIN) && denied == 0) {
        /* TODO: Check if current process has admin privilege */
        /* For now, deny if ADMIN flag is set for write operations */
        if (operation != SIP_OP_READ) {
            denied = 1;  /* Would need admin auth */
        }
    }
    
    /* Log violation if denied */
    if (denied) {
        sip_violation_t violation = {
            .path = path,
            .operation = operation == SIP_OP_WRITE ? "write" : 
                        operation == SIP_OP_DELETE ? "delete" :
                        operation == SIP_OP_CREATE ? "create" :
                        operation == SIP_OP_MKDIR ? "mkdir" :
                        operation == SIP_OP_RENAME ? "rename" : "unknown",
            .pid = get_current_pid(),
            .protection = prot,
            .timestamp = pit_get_ticks()
        };
        sip_log_violation(&violation);
        
        /* Permissive mode - log but allow */
        if (g_sip_status == SIP_PERMISSIVE) {
            return 0;
        }
        
        return -13;  /* EACCES */
    }
    
    return 0;
}

int sip_check_writable(const char *path) {
    return sip_check_path(path, SIP_OP_WRITE) == 0 ? 1 : 0;
}

int sip_add_rule(const sip_rule_t *rule) {
    if (!rule || g_rule_count >= SIP_MAX_RULES) {
        return -1;
    }
    
    /* Only in recovery mode */
    if (!(boot_guard_get_mode() & BOOT_MODE_RECOVERY)) {
        serial_puts("[SIP] Error: Can only add rules in recovery mode\n");
        return -1;
    }
    
    g_rules[g_rule_count++] = *rule;
    
    serial_puts("[SIP] Added rule for: ");
    serial_puts(rule->path);
    serial_puts("\n");
    
    return 0;
}

void sip_log_violation(const sip_violation_t *violation) {
    g_violation_count++;
    
    serial_puts("[SIP] *** VIOLATION #");
    serial_dec(g_violation_count);
    serial_puts(" ***\n");
    serial_puts("[SIP]   Path: ");
    if (violation->path) serial_puts(violation->path);
    serial_puts("\n");
    serial_puts("[SIP]   Operation: ");
    if (violation->operation) serial_puts(violation->operation);
    serial_puts("\n");
    serial_puts("[SIP]   PID: ");
    serial_dec(violation->pid);
    serial_puts("\n");
    serial_puts("[SIP]   Protection: 0x");
    const char hex[] = "0123456789ABCDEF";
    serial_putc(hex[(violation->protection >> 4) & 0xF]);
    serial_putc(hex[violation->protection & 0xF]);
    serial_puts("\n");
}

uint32_t sip_get_violation_count(void) {
    return g_violation_count;
}

void sip_print_status(void) {
    serial_puts("\n=== System Integrity Protection ===\n");
    
    serial_puts("  Status: ");
    switch (g_sip_status) {
        case SIP_ENABLED:
            serial_puts("ENABLED");
            break;
        case SIP_DISABLED:
            serial_puts("DISABLED");
            break;
        case SIP_PERMISSIVE:
            serial_puts("PERMISSIVE (logging only)");
            break;
    }
    serial_puts("\n");
    
    serial_puts("  Rules: ");
    serial_dec(g_rule_count);
    serial_puts("\n");
    
    serial_puts("  Violations: ");
    serial_dec(g_violation_count);
    serial_puts("\n");
    
    serial_puts("  Protected paths:\n");
    for (int i = 0; i < g_rule_count && i < 8; i++) {
        if (g_rules[i].protection != SIP_PROT_NONE) {
            serial_puts("    ");
            serial_puts(g_rules[i].path);
            serial_puts(" [");
            if (g_rules[i].protection & SIP_PROT_READ_ONLY) serial_puts("RO ");
            if (g_rules[i].protection & SIP_PROT_NO_DELETE) serial_puts("ND ");
            if (g_rules[i].protection & SIP_PROT_SYSTEM) serial_puts("SYS ");
            if (g_rules[i].protection & SIP_PROT_ADMIN) serial_puts("ADM ");
            serial_puts("]\n");
        }
    }
    
    serial_puts("===================================\n\n");
}
