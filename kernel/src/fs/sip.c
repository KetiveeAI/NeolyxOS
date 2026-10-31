/*
 * NeolyxOS System Integrity Protection (SIP) Implementation
 * 
 * Protects critical system files from modification.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include <stdint.h>
#include "../../include/fs/sip.h"
#include "core/process.h"

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
static int g_sip_ready = 0;          /* rules loaded by sip_init() */

/* Maximum rules */
#define SIP_MAX_RULES 48
static sip_rule_t g_rules[SIP_MAX_RULES];
static int g_rule_count = 0;

/* Longest path SIP will judge; longer paths are refused, not guessed at */
#define SIP_PATH_MAX 512

/* ============ Default Protected Paths ============ */

/*
 * Matching is case-insensitive (the boot volume is FAT, which is) and
 * boundary-aware: "/System" matches "/System" and "/System/x" but not
 * "/SystemX". recursive = 0 makes a rule match the path itself only, and
 * an exact rule beats a recursive rule for the same path.
 *
 * Who the flags stop (see sip_check_path):
 *   READ_ONLY  - every userspace process, system ones included
 *   NO_DELETE  - every userspace process (delete and rename)
 *   SYSTEM     - ordinary user processes; system processes may modify
 *   ADMIN      - ordinary user processes until an accounts service exists
 * Kernel-internal callers (installer, fs_init) are never blocked.
 *
 * A top-level entry with no rule of its own ("/foo") is treated as SYSTEM:
 * user programs cannot add or remove things in the root directory.
 */
static const sip_rule_t g_default_rules[] = {
    /* Core system - nobody in userspace may change it */
    { "/System",        SIP_PROT_READ_ONLY | SIP_PROT_NO_DELETE, 1 },
    { "/Kernel",        SIP_PROT_READ_ONLY | SIP_PROT_NO_DELETE, 1 },
    { "/Boot",          SIP_PROT_READ_ONLY | SIP_PROT_NO_DELETE, 1 },
    { "/EFI",           SIP_PROT_READ_ONLY | SIP_PROT_NO_DELETE, 1 },
    { "/kernel.bin",    SIP_PROT_READ_ONLY | SIP_PROT_NO_DELETE, 1 },
    { "/INSTALLED.MRK", SIP_PROT_READ_ONLY | SIP_PROT_NO_DELETE, 1 },
    { "/NvVars",        SIP_PROT_READ_ONLY | SIP_PROT_NO_DELETE, 1 },
    { "/ramfs",         SIP_PROT_READ_ONLY | SIP_PROT_NO_DELETE, 1 },
    { "/apps",          SIP_PROT_READ_ONLY | SIP_PROT_NO_DELETE, 1 },
    { "/bin",           SIP_PROT_READ_ONLY | SIP_PROT_NO_DELETE, 1 },
    { "/sbin",          SIP_PROT_READ_ONLY | SIP_PROT_NO_DELETE, 1 },
    
    /* System settings - the desktop (a system process) saves them */
    { "/System/Config", SIP_PROT_SYSTEM | SIP_PROT_NO_DELETE, 0 },
    { "/System/Config", SIP_PROT_SYSTEM, 1 },
    
    /* Libraries and apps - system processes and (later) admins */
    { "/Library",       SIP_PROT_SYSTEM | SIP_PROT_ADMIN | SIP_PROT_NO_DELETE, 0 },
    { "/Library",       SIP_PROT_SYSTEM | SIP_PROT_ADMIN, 1 },
    { "/Applications",  SIP_PROT_ADMIN | SIP_PROT_NO_DELETE, 0 },
    { "/Applications",  SIP_PROT_ADMIN, 1 },
    
    /* User areas - the folder itself stays, its contents are the user's */
    { "/Users",         SIP_PROT_SYSTEM | SIP_PROT_NO_DELETE, 0 },
    { "/Users",         SIP_PROT_NONE, 1 },
    { "/home",          SIP_PROT_SYSTEM | SIP_PROT_NO_DELETE, 0 },
    { "/home",          SIP_PROT_NONE, 1 },
    { "/tmp",           SIP_PROT_SYSTEM | SIP_PROT_NO_DELETE, 0 },
    { "/tmp",           SIP_PROT_NONE, 1 },
    { "/var/tmp",       SIP_PROT_NONE, 1 },
    
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

static char sip_lower(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

/*
 * sip_normalize - Canonical form of an absolute path for rule matching.
 * Collapses repeated slashes, drops "." and resolves "..", the same way
 * the VFS walks them, so "/Users/../System" and "//System/./x" cannot
 * slip past a "/System" rule. Returns 0 on success, -1 if the path is
 * not absolute or does not fit.
 */
static int sip_normalize(const char *in, char *out, int cap) {
    if (!in || !out || cap < 2 || in[0] != '/') return -1;
    int o = 0;
    out[o++] = '/';
    const char *p = in;
    while (*p) {
        while (*p == '/') p++;
        if (!*p) break;
        const char *s = p;
        while (*p && *p != '/') p++;
        int n = (int)(p - s);
        if (n == 1 && s[0] == '.') continue;
        if (n == 2 && s[0] == '.' && s[1] == '.') {
            /* Drop the last component; ".." at the root stays at the root */
            if (o > 1) {
                o--;
                while (o > 1 && out[o - 1] != '/') o--;
                if (o > 1) o--;          /* remove the separator too */
            }
            continue;
        }
        if (o > 1) {
            if (o + 1 >= cap) return -1;
            out[o++] = '/';
        }
        if (o + n >= cap) return -1;
        for (int i = 0; i < n; i++) out[o++] = s[i];
    }
    out[o] = '\0';
    return 0;
}

/*
 * Does @path (normalized) fall under @rule? Case-insensitive; a recursive
 * rule matches the path and everything below it, an exact rule only the
 * path itself. Returns the rule length on a match, 0 otherwise.
 */
static int sip_rule_match(const char *path, const sip_rule_t *rule) {
    if (!path || !rule || !rule->path) return 0;
    const char *a = path;
    const char *b = rule->path;
    int len = 0;
    while (*b) {
        if (sip_lower(*a) != sip_lower(*b)) return 0;
        a++; b++; len++;
    }
    if (*a == '\0') return len;
    if (*a == '/' && rule->recursive) return len;
    return 0;
}

/* ============ Caller Classification ============ */

#define SIP_CALLER_KERNEL 0
#define SIP_CALLER_SYSTEM 1
#define SIP_CALLER_USER   2

/*
 * Who is asking? Kernel threads (no user address space) and kernel-
 * internal callers such as the installer are trusted. A user process
 * spawned with PROC_FLAG_SYSTEM (the desktop) is a system process.
 * Everything else is an ordinary user process.
 */
static int sip_caller_class(void) {
    process_t *p = process_current();
    if (!p || !(p->flags & PROC_FLAG_USER)) return SIP_CALLER_KERNEL;
    if (p->flags & PROC_FLAG_SYSTEM) return SIP_CALLER_SYSTEM;
    return SIP_CALLER_USER;
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
    for (int i = 0; g_default_rules[i].path != NULL && g_rule_count < SIP_MAX_RULES; i++) {
        g_rules[g_rule_count++] = g_default_rules[i];
    }
    g_sip_ready = 1;
    
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

/* Protection for an already-normalized path. */
static uint8_t sip_protection_of(const char *norm) {
    const sip_rule_t *best = NULL;
    int best_score = 0;
    
    for (int i = 0; i < g_rule_count; i++) {
        int len = sip_rule_match(norm, &g_rules[i]);
        if (len == 0) continue;
        /* Longer rule wins; for the same path an exact rule wins */
        int score = len * 2 + (g_rules[i].recursive ? 0 : 1);
        if (score > best_score) {
            best_score = score;
            best = &g_rules[i];
        }
    }
    if (best) return best->protection;
    
    /* The root itself, and unlisted entries directly in it */
    if (norm[0] == '/' && norm[1] == '\0') {
        return SIP_PROT_READ_ONLY | SIP_PROT_NO_DELETE;
    }
    int slashes = 0;
    for (const char *p = norm; *p; p++) if (*p == '/') slashes++;
    if (slashes == 1) return SIP_PROT_SYSTEM;
    
    return SIP_PROT_NONE;
}

uint8_t sip_get_protection(const char *path) {
    char norm[SIP_PATH_MAX];
    if (sip_normalize(path, norm, (int)sizeof(norm)) != 0) {
        /* Unjudgeable paths are treated as protected */
        return SIP_PROT_READ_ONLY | SIP_PROT_NO_DELETE;
    }
    return sip_protection_of(norm);
}

static const char *sip_op_name(int operation) {
    switch (operation) {
        case SIP_OP_WRITE:  return "write";
        case SIP_OP_DELETE: return "delete";
        case SIP_OP_CREATE: return "create";
        case SIP_OP_MKDIR:  return "mkdir";
        case SIP_OP_RENAME: return "rename";
        case SIP_OP_CHMOD:  return "chmod";
        case SIP_OP_CHOWN:  return "chown";
        default:            return "unknown";
    }
}

int sip_check_path(const char *path, int operation) {
    /* Read operations always allowed */
    if (operation == SIP_OP_READ) {
        return 0;
    }
    
    /* Before sip_init (early boot) and in recovery mode nothing is enforced */
    if (!g_sip_ready || g_sip_status == SIP_DISABLED) {
        return 0;
    }
    
    /* The kernel itself (installer, fs_init, ...) is trusted */
    int caller = sip_caller_class();
    if (caller == SIP_CALLER_KERNEL) {
        return 0;
    }
    
    char norm[SIP_PATH_MAX];
    uint8_t prot;
    if (!path || sip_normalize(path, norm, (int)sizeof(norm)) != 0) {
        norm[0] = '\0';
        prot = SIP_PROT_READ_ONLY | SIP_PROT_NO_DELETE;
    } else {
        prot = sip_protection_of(norm);
    }
    
    /* No protection - allow */
    if (prot == SIP_PROT_NONE) {
        return 0;
    }
    
    int removing = (operation == SIP_OP_DELETE || operation == SIP_OP_RENAME);
    int denied = 0;
    
    if (prot & SIP_PROT_IMMUTABLE) denied = 1;
    if (prot & SIP_PROT_READ_ONLY) denied = 1;
    if (removing && (prot & SIP_PROT_NO_DELETE)) denied = 1;
    if (caller == SIP_CALLER_USER) {
        if (prot & SIP_PROT_SYSTEM) denied = 1;
        /* No accounts service yet, so nobody can prove admin rights */
        if (prot & SIP_PROT_ADMIN) denied = 1;
    }
    
    if (!denied) {
        return 0;
    }
    
    sip_violation_t violation = {
        .path = norm[0] ? norm : (path ? path : "(null)"),
        .operation = sip_op_name(operation),
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
    for (int i = 0; i < g_rule_count; i++) {
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
