/*
 * NeolyxOS Kernel Security Module Implementation
 * 
 * Capability-based security with audit logging.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include <stdint.h>
#include "../include/core/security.h"

/* ============ External Dependencies ============ */

extern void serial_puts(const char *s);
extern void serial_putc(char c);
extern uint32_t get_current_pid(void);

/* ============ Static State ============ */

static uint8_t g_security_initialized = 0;

/* Audit log ring buffer */
#define AUDIT_LOG_SIZE 64
static struct {
    audit_event_t event;
    int32_t pid;
    uint64_t timestamp;
    char details[64];
} g_audit_log[AUDIT_LOG_SIZE];
static uint32_t g_audit_index = 0;

/* Default security context (kernel mode) */
static security_context_t g_kernel_context = {
    .uid = 0,
    .gid = 0,
    .priv = PRIV_KERNEL,
    .capabilities = CAP_ALL,
    .session_id = 0,
};

/* Current process context (simplified - would come from process table) */
static security_context_t g_current_context = {
    .uid = 1000,
    .gid = 1000,
    .priv = PRIV_USER,
    .capabilities = CAP_NONE,
    .session_id = 1,
};

/* ============ Utility Functions ============ */

static void sec_strcpy(char *dst, const char *src, int max) {
    int i = 0;
    while (src && src[i] && i < max - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void print_hex(uint32_t val) {
    const char hex[] = "0123456789ABCDEF";
    char buf[9];
    for (int i = 7; i >= 0; i--) {
        buf[i] = hex[val & 0xF];
        val >>= 4;
    }
    buf[8] = '\0';
    serial_puts(buf);
}

static void print_dec(int32_t val) {
    if (val < 0) {
        serial_putc('-');
        val = -val;
    }
    char buf[12];
    int i = 0;
    do {
        buf[i++] = '0' + (val % 10);
        val /= 10;
    } while (val > 0);
    while (i > 0) serial_putc(buf[--i]);
}

/* ============ Security API Implementation ============ */

int security_init(void) {
    if (g_security_initialized) return 0;
    
    serial_puts("[SECURITY] Initializing kernel security module...\n");
    
    /* Clear audit log */
    for (int i = 0; i < AUDIT_LOG_SIZE; i++) {
        g_audit_log[i].event = AUDIT_NONE;
        g_audit_log[i].pid = -1;
        g_audit_log[i].timestamp = 0;
        g_audit_log[i].details[0] = '\0';
    }
    g_audit_index = 0;
    
    /* Log initialization */
    security_audit_log(AUDIT_SYSCALL, 0, "Security module initialized");
    
    g_security_initialized = 1;
    serial_puts("[SECURITY] Security module ready\n");
    return 0;
}

int security_check_capability(const security_context_t *ctx, uint32_t cap) {
    /* Null check */
    if (!ctx) {
        security_audit_log(AUDIT_DENIED, -1, "NULL context in cap check");
        return -EPERM;
    }
    
    /* Kernel always has all caps */
    if (ctx->priv == PRIV_KERNEL) {
        return 0;
    }
    
    /* Check if capability is present */
    if ((ctx->capabilities & cap) == cap) {
        return 0;
    }
    
    /* Permission denied - log it */
    security_audit_log(AUDIT_DENIED, get_current_pid(), "Capability denied");
    return -EPERM;
}

int security_check_privilege(const security_context_t *ctx, privilege_level_t required) {
    /* Null check */
    if (!ctx) {
        security_audit_log(AUDIT_DENIED, -1, "NULL context in priv check");
        return -EPERM;
    }
    
    /* Check privilege level */
    if (ctx->priv >= required) {
        return 0;
    }
    
    /* Privilege escalation attempt */
    security_audit_log(AUDIT_PRIV_ESCALATION, get_current_pid(), "Privilege check failed");
    return -EPERM;
}

int security_validate_pointer(const void *ptr, uint64_t size, int write) {
    (void)write;
    
    /* Null pointer check */
    if (!ptr) {
        security_audit_log(AUDIT_SECURITY_VIOLATION, get_current_pid(), "NULL pointer access");
        return -EFAULT;
    }
    
    /* Size overflow check */
    if (size == 0 || size > 0x100000000ULL) {
        security_audit_log(AUDIT_SECURITY_VIOLATION, get_current_pid(), "Invalid size");
        return -EINVAL;
    }
    
    /* Check pointer is in userspace range (simplified) */
    uint64_t addr = (uint64_t)ptr;
    
    /* Kernel addresses (above 0xFFFF800000000000 on x86_64) are off-limits */
    if (addr >= 0xFFFF800000000000ULL) {
        security_audit_log(AUDIT_SECURITY_VIOLATION, get_current_pid(), "Kernel addr access");
        return -EFAULT;
    }
    
    /* Check for overflow */
    if (addr + size < addr) {
        security_audit_log(AUDIT_SECURITY_VIOLATION, get_current_pid(), "Pointer overflow");
        return -EFAULT;
    }
    
    return 0;
}

int security_validate_buffer(const void *buf, uint64_t buf_size, uint64_t access_size) {
    /* Null check */
    if (!buf) {
        return -EFAULT;
    }
    
    /* Bounds check */
    if (access_size > buf_size) {
        security_audit_log(AUDIT_SECURITY_VIOLATION, get_current_pid(), "Buffer overflow attempt");
        return -EINVAL;
    }
    
    /* Overflow check */
    if (access_size > 0x100000000ULL) {
        return -EINVAL;
    }
    
    return 0;
}

void security_audit_log(audit_event_t event, int32_t pid, const char *details) {
    /* Store in ring buffer */
    uint32_t idx = g_audit_index % AUDIT_LOG_SIZE;
    g_audit_log[idx].event = event;
    g_audit_log[idx].pid = pid;
    g_audit_log[idx].timestamp = 0; /* TODO: Get real timestamp */
    
    if (details) {
        sec_strcpy(g_audit_log[idx].details, details, 64);
    } else {
        g_audit_log[idx].details[0] = '\0';
    }
    
    g_audit_index++;
    
    /* Print to serial for debugging */
    static const char *event_names[] = {
        "NONE", "LOGIN", "LOGOUT", "EXEC", "FILE_ACCESS",
        "FILE_MODIFY", "PRIV_ESCALATION", "DENIED", "SYSCALL",
        "MODULE_LOAD", "NETWORK", "SECURITY_VIOLATION"
    };
    
    if (event > 0 && event <= AUDIT_SECURITY_VIOLATION) {
        serial_puts("[AUDIT] ");
        serial_puts(event_names[event]);
        serial_puts(" PID=");
        print_dec(pid);
        if (details) {
            serial_puts(" ");
            serial_puts(details);
        }
        serial_puts("\n");
    }
}

security_context_t *security_get_context(void) {
    /* In real implementation, get from current process */
    return &g_current_context;
}

security_context_t *security_create_context(uint32_t uid, uint32_t gid,
                                            privilege_level_t priv, uint32_t caps) {
    /* Validate privilege escalation */
    security_context_t *current = security_get_context();
    
    /* Cannot create context with higher privilege than own */
    if (current && priv > current->priv) {
        security_audit_log(AUDIT_PRIV_ESCALATION, get_current_pid(), "Context creation denied");
        return NULL;
    }
    
    /* For now, return kernel context for kernel requests */
    if (priv == PRIV_KERNEL && uid == 0) {
        return &g_kernel_context;
    }
    
    /* Update current context (simplified) */
    g_current_context.uid = uid;
    g_current_context.gid = gid;
    g_current_context.priv = priv;
    g_current_context.capabilities = caps;
    
    security_audit_log(AUDIT_EXEC, get_current_pid(), "Context created");
    return &g_current_context;
}

void security_print_status(void) {
    serial_puts("\n=== Security Module Status ===\n");
    serial_puts("Initialized: ");
    serial_puts(g_security_initialized ? "Yes" : "No");
    serial_puts("\n");
    
    serial_puts("Current context:\n");
    serial_puts("  UID: ");
    print_dec(g_current_context.uid);
    serial_puts("\n  GID: ");
    print_dec(g_current_context.gid);
    serial_puts("\n  Privilege: ");
    print_dec(g_current_context.priv);
    serial_puts("\n  Capabilities: 0x");
    print_hex(g_current_context.capabilities);
    serial_puts("\n");
    
    serial_puts("Audit events logged: ");
    print_dec(g_audit_index);
    serial_puts("\n");
}
