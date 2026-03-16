/*
 * NeolyxOS ABI Definitions
 * 
 * Kernel-userspace binary interface versioning and syscall dispatch.
 * 
 * Copyright (c) 2025 KetiveeAI. All rights reserved.
 */

#ifndef NEOLYX_NX_ABI_H
#define NEOLYX_NX_ABI_H

#include <stdint.h>

/* ABI Version - Semver style */
#define NX_ABI_MAJOR  1
#define NX_ABI_MINOR  0
#define NX_ABI_PATCH  0

/* Maximum syscall number supported */
#define NX_SYSCALL_MAX  256

/* Syscall flags */
#define NX_FLAG_AUDIT      0x0001  /* Log all calls to this syscall */
#define NX_FLAG_DEPRECATED 0x0002  /* Warn on use */
#define NX_FLAG_ASYNC      0x0004  /* Non-blocking allowed */

/* ABI info structure (returned by syscall 0) */
typedef struct {
    uint16_t major;          /* Breaking changes */
    uint16_t minor;          /* New features */
    uint16_t patch;          /* Bug fixes */
    uint16_t flags;          /* Kernel capability flags */
    uint32_t syscall_count;  /* Total registered syscalls */
    uint32_t reserved[3];    /* Future use */
} nx_abi_info_t;

/* Per-process syscall dispatch control */
typedef struct {
    uint8_t  filter_mode;    /* NX_DISPATCH_OFF, NX_DISPATCH_ALLOW */
    uint8_t  allowed[32];    /* Bitmask for 256 syscalls */
    uint32_t blocked_count;  /* Statistics: blocked calls */
    uint32_t total_count;    /* Statistics: total calls */
} nx_syscall_dispatch_t;

/* Dispatch modes */
#define NX_DISPATCH_OFF   0  /* No filtering */
#define NX_DISPATCH_ALLOW 1  /* Only allowed syscalls */

/* Syscall descriptor (kernel-internal) */
typedef struct {
    int64_t (*handler)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
    const char *name;
    uint8_t  argc;
    uint8_t  priv;
    uint16_t flags;
} nx_syscall_desc_t;

/* ABI syscall implementations */
int64_t sys_abi_query_impl(uint64_t info_ptr, uint64_t, uint64_t, uint64_t, uint64_t);
int64_t sys_abi_negotiate_impl(uint64_t req_major, uint64_t req_minor,
                               uint64_t max_major, uint64_t max_minor, uint64_t);

/* Syscall name lookup (for debugging) */
const char *nx_syscall_name(int num);

/* Per-process filter control */
int nx_set_syscall_filter(void *proc, int mode, const uint8_t *allowed_mask);
int nx_syscall_allowed(void *proc, uint64_t num);

/* Audit logging */
#define NX_AUDIT_CALL    0  /* Syscall was made */
#define NX_AUDIT_BLOCKED 1  /* Syscall was blocked */
void nx_audit_log(int pid, uint64_t syscall_num, int event);

#endif /* NEOLYX_NX_ABI_H */
