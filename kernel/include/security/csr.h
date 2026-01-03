/*
 * NeolyxOS Configurable Security Restrictions (CSR)
 * 
 * Copyright (c) 2025 KetiveeAI. All rights reserved.
 * 
 * Kernel-level security configuration flags inspired by macOS SIP/CSR.
 * Controls system integrity protections that can only be modified
 * from recovery mode.
 */

#ifndef _NEOLYX_CSR_H
#define _NEOLYX_CSR_H

#include <stdint.h>
#include <stdbool.h>

/*
 * CSR flag definitions
 * Each flag allows a specific security restriction to be bypassed
 */
#define CSR_ALLOW_UNTRUSTED_KEXTS       (1 << 0)  /* Load unsigned kernel extensions */
#define CSR_ALLOW_TASK_FOR_PID          (1 << 1)  /* Debug arbitrary processes */
#define CSR_ALLOW_KERNEL_DEBUGGER       (1 << 2)  /* Attach kernel debugger */
#define CSR_ALLOW_UNRESTRICTED_FS       (1 << 3)  /* Modify protected paths */
#define CSR_ALLOW_DEVICE_CONFIG         (1 << 4)  /* Change device configuration */
#define CSR_ALLOW_UNRESTRICTED_NVRAM    (1 << 5)  /* Modify system NVRAM */
#define CSR_ALLOW_UNRESTRICTED_DTRACE   (1 << 6)  /* Unrestricted DTrace access */
#define CSR_ALLOW_UNAUTHENTICATED_ROOT  (1 << 7)  /* Boot from unsigned root */

/* 
 * Common configurations
 */
#define CSR_ENABLED             0x00000000  /* All protections enabled */
#define CSR_DISABLED            0x000000FF  /* All protections disabled */
#define CSR_DEVELOPER_MODE      (CSR_ALLOW_TASK_FOR_PID | CSR_ALLOW_UNRESTRICTED_DTRACE)

/*
 * CSR configuration source
 */
typedef enum {
    CSR_SOURCE_DEFAULT = 0,     /* Default secure configuration */
    CSR_SOURCE_BOOT = 1,        /* Boot-time parameter */
    CSR_SOURCE_RECOVERY = 2,    /* Set from recovery mode */
    CSR_SOURCE_NVRAM = 3        /* Read from NVRAM */
} csr_source_t;

/*
 * CSR configuration state
 */
typedef struct {
    uint32_t flags;             /* Active CSR flags */
    csr_source_t source;        /* How this config was set */
    bool recovery_mode;         /* Currently in recovery mode */
    uint64_t set_time;          /* Timestamp when set */
} csr_config_t;

/* Initialize CSR subsystem */
void csr_init(void);

/* Check if a CSR flag is set (restriction bypassed) */
bool csr_check(uint32_t flag);

/* Get active CSR configuration */
csr_config_t csr_get_config(void);

/* Get raw flags value */
uint32_t csr_get_active_flags(void);

/* Set CSR configuration (only in recovery mode) */
int csr_set_config(uint32_t flags);

/* Enter recovery mode (allows CSR modification) */
void csr_enter_recovery(void);

/* Exit recovery mode and lock CSR */
void csr_exit_recovery(void);

/* Check if in recovery mode */
bool csr_in_recovery(void);

/* Display CSR status */
void csr_status(void);

#endif /* _NEOLYX_CSR_H */
