/*
 * NeolyxOS Kernel Security Module
 * 
 * Provides capability-based security, privilege levels, and audit logging.
 * Prevents unauthorized access and tracks security events.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef NEOLYX_SECURITY_H
#define NEOLYX_SECURITY_H

#include <stdint.h>

/* ============ Privilege Levels ============ */

typedef enum {
    PRIV_USER    = 0,   /* Normal user processes */
    PRIV_ADMIN   = 1,   /* Administrative processes */
    PRIV_SYSTEM  = 2,   /* System services */
    PRIV_KERNEL  = 3,   /* Kernel mode only */
} privilege_level_t;

/* ============ Capability Flags ============ */

#define CAP_NONE           0x00000000
#define CAP_SYS_ADMIN      0x00000001  /* System administration */
#define CAP_NET_RAW        0x00000002  /* Raw network access */
#define CAP_NET_BIND       0x00000004  /* Bind to privileged ports */
#define CAP_SYS_MOUNT      0x00000008  /* Mount filesystems */
#define CAP_SYS_MODULE     0x00000010  /* Load kernel modules */
#define CAP_SYS_RAWIO      0x00000020  /* Direct I/O access */
#define CAP_SYS_REBOOT     0x00000040  /* Reboot/shutdown system */
#define CAP_FILE_WRITE     0x00000080  /* Write to system files */
#define CAP_PROC_KILL      0x00000100  /* Kill any process */
#define CAP_MEM_LOCK       0x00000200  /* Lock memory pages */
#define CAP_DEBUG          0x00000400  /* Debug other processes */
#define CAP_AUDIT          0x00000800  /* Access audit logs */

#define CAP_ALL            0xFFFFFFFF  /* All capabilities (kernel only) */

/* ============ Security Context ============ */

typedef struct {
    uint32_t uid;               /* User ID */
    uint32_t gid;               /* Group ID */
    privilege_level_t priv;     /* Privilege level */
    uint32_t capabilities;      /* Capability bitmask */
    uint32_t session_id;        /* Login session ID */
} security_context_t;

/* ============ Audit Event Types ============ */

typedef enum {
    AUDIT_NONE = 0,
    AUDIT_LOGIN,                /* User login */
    AUDIT_LOGOUT,               /* User logout */
    AUDIT_EXEC,                 /* Process execution */
    AUDIT_FILE_ACCESS,          /* File access */
    AUDIT_FILE_MODIFY,          /* File modification */
    AUDIT_PRIV_ESCALATION,      /* Privilege escalation attempt */
    AUDIT_DENIED,               /* Permission denied */
    AUDIT_SYSCALL,              /* Privileged syscall */
    AUDIT_MODULE_LOAD,          /* Module loaded */
    AUDIT_NETWORK,              /* Network event */
    AUDIT_SECURITY_VIOLATION,   /* Violation detected */
} audit_event_t;

/* ============ Security API ============ */

/**
 * security_init - Initialize security subsystem
 * 
 * Returns: 0 on success
 */
int security_init(void);

/**
 * security_check_capability - Check if process has capability
 * 
 * @ctx: Security context
 * @cap: Required capability flag
 * 
 * Returns: 0 if allowed, -EPERM if denied
 */
int security_check_capability(const security_context_t *ctx, uint32_t cap);

/**
 * security_check_privilege - Check minimum privilege level
 * 
 * @ctx: Security context
 * @required: Minimum privilege level
 * 
 * Returns: 0 if allowed, -EPERM if denied
 */
int security_check_privilege(const security_context_t *ctx, privilege_level_t required);

/**
 * security_validate_pointer - Validate userspace pointer
 * 
 * @ptr: Pointer to validate
 * @size: Size of access
 * @write: 1 if write access, 0 for read
 * 
 * Returns: 0 if valid, -EFAULT if invalid
 */
int security_validate_pointer(const void *ptr, uint64_t size, int write);

/**
 * security_validate_buffer - Validate buffer bounds
 * 
 * @buf: Buffer pointer
 * @buf_size: Buffer size
 * @access_size: Requested access size
 * 
 * Returns: 0 if valid, -EINVAL if overflow
 */
int security_validate_buffer(const void *buf, uint64_t buf_size, uint64_t access_size);

/**
 * security_audit_log - Log security event
 * 
 * @event: Event type
 * @pid: Process ID
 * @details: Event details string (can be NULL)
 */
void security_audit_log(audit_event_t event, int32_t pid, const char *details);

/**
 * security_get_context - Get current process security context
 * 
 * Returns: Pointer to context or NULL
 */
security_context_t *security_get_context(void);

/**
 * security_create_context - Create new security context
 * 
 * @uid: User ID
 * @gid: Group ID
 * @priv: Privilege level
 * @caps: Capabilities
 * 
 * Returns: New context or NULL
 */
security_context_t *security_create_context(uint32_t uid, uint32_t gid,
                                            privilege_level_t priv, uint32_t caps);

/**
 * security_print_status - Print security subsystem status
 */
void security_print_status(void);

/* ============ Error Codes ============ */

#define EPERM   1   /* Operation not permitted */
#define EFAULT  14  /* Bad address */
#define EINVAL  22  /* Invalid argument */
#define EACCES  13  /* Permission denied */

#endif /* NEOLYX_SECURITY_H */
