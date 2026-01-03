/*
 * NeolyxOS Task Port Protection
 * 
 * Copyright (c) 2025 KetiveeAI. All rights reserved.
 * 
 * Process hardening to prevent unauthorized debugging,
 * code injection, and memory inspection.
 */

#ifndef _NEOLYX_TASK_PORT_H
#define _NEOLYX_TASK_PORT_H

#include <stdint.h>
#include <stdbool.h>

/*
 * Process protection levels
 */
typedef enum {
    TASK_PROT_STANDARD = 0,     /* Normal user processes */
    TASK_PROT_HARDENED = 1,     /* Hardened runtime - limited debugging */
    TASK_PROT_SYSTEM = 2        /* System processes - no debugging */
} task_protection_t;

/*
 * Inspection request types
 */
typedef enum {
    TASK_INSPECT_MEMORY = 0,    /* Read/write process memory */
    TASK_INSPECT_REGISTERS = 1, /* Read CPU registers */
    TASK_INSPECT_ATTACH = 2,    /* Attach debugger */
    TASK_INSPECT_INJECT = 3,    /* Inject code/library */
    TASK_INSPECT_SIGNAL = 4     /* Send signal */
} task_inspect_type_t;

/*
 * Inspection result
 */
typedef enum {
    TASK_INSPECT_ALLOW = 0,
    TASK_INSPECT_DENY = 1,
    TASK_INSPECT_AUDIT = 2      /* Allowed but logged */
} task_inspect_result_t;

/*
 * Process runtime flags
 */
#define TASK_FLAG_HARDENED_RUNTIME  (1 << 0)
#define TASK_FLAG_LIBRARY_VALIDATION (1 << 1)
#define TASK_FLAG_SANDBOX_ENFORCED  (1 << 2)
#define TASK_FLAG_ALLOW_DYLD_ENV    (1 << 3)

/* Initialize task port protection */
void task_port_init(void);

/* Set protection level for a process */
int task_port_set_protection(uint32_t pid, task_protection_t level);

/* Get protection level for a process */
task_protection_t task_port_get_protection(uint32_t pid);

/* Check if inspection is allowed */
task_inspect_result_t task_port_check_inspection(
    uint32_t inspector_pid,
    uint32_t target_pid,
    task_inspect_type_t type
);

/* Set hardened runtime flags */
int task_port_set_flags(uint32_t pid, uint32_t flags);

/* Get hardened runtime flags */
uint32_t task_port_get_flags(uint32_t pid);

/* Allow specific process to inspect another (admin) */
int task_port_grant_access(uint32_t inspector_pid, uint32_t target_pid);

/* Revoke inspection access */
int task_port_revoke_access(uint32_t inspector_pid, uint32_t target_pid);

/* Check if process has task-for-pid capability */
bool task_port_has_capability(uint32_t pid);

/* Display task port status */
void task_port_status(void);

#endif /* _NEOLYX_TASK_PORT_H */
