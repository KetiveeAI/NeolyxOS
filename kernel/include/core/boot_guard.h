/*
 * NeolyxOS Boot Guard
 * 
 * Crash-proof boot sequence protection.
 * Tracks boot status, detects failures, triggers recovery.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef NEOLYX_BOOT_GUARD_H
#define NEOLYX_BOOT_GUARD_H

#include <stdint.h>

/* Boot status values (stored in boot partition or NVRAM) */
typedef enum {
    BOOT_STATUS_UNKNOWN     = 0x00,   /* First boot or corrupted */
    BOOT_STATUS_BOOTING     = 0x01,   /* Kernel is starting up */
    BOOT_STATUS_DRIVERS_OK  = 0x02,   /* Drivers loaded successfully */
    BOOT_STATUS_FS_OK       = 0x03,   /* Filesystem mounted */
    BOOT_STATUS_GUI_OK      = 0x04,   /* GUI started (full boot) */
    BOOT_STATUS_OK          = 0xFF,   /* Boot completed successfully */
    BOOT_STATUS_FAILED      = 0xFE,   /* Boot failed, needs recovery */
} boot_status_t;

/* Boot mode flags */
typedef enum {
    BOOT_MODE_NORMAL        = 0x00,   /* Normal boot */
    BOOT_MODE_SAFE          = 0x01,   /* Safe mode (minimal drivers) */
    BOOT_MODE_RECOVERY      = 0x02,   /* Recovery mode */
    BOOT_MODE_VERBOSE       = 0x04,   /* Verbose logging */
} boot_mode_flags_t;

/* Boot guard state */
typedef struct {
    uint32_t magic;                   /* 0x424F4F54 "BOOT" */
    uint8_t  status;                  /* Current boot status */
    uint8_t  mode;                    /* Boot mode flags */
    uint8_t  failure_count;           /* Consecutive boot failures */
    uint8_t  reserved;
    uint64_t last_boot_time;          /* Timestamp of last boot attempt */
    uint64_t last_success_time;       /* Timestamp of last successful boot */
    uint32_t watchdog_timeout_ms;     /* Watchdog timeout in milliseconds */
    uint32_t checksum;                /* CRC32 of this structure */
} boot_guard_state_t;

/* Configuration */
#define BOOT_GUARD_MAGIC            0x424F4F54  /* "BOOT" */
#define BOOT_GUARD_MAX_FAILURES     3           /* Trigger recovery after this many */
#define BOOT_GUARD_TIMEOUT_MS       30000       /* 30 second boot timeout */
#define BOOT_GUARD_WATCHDOG_MS      5000        /* 5 second watchdog interval */

/* ============ Boot Guard API ============ */

/*
 * boot_guard_init - Initialize boot guard subsystem
 * 
 * Called very early in kernel boot.
 * Loads previous boot state and checks for failures.
 * 
 * Returns: 0 on success, -1 on error
 */
int boot_guard_init(void);

/*
 * boot_guard_mark_stage - Mark a boot stage as complete
 * 
 * @status: The stage that completed (BOOT_STATUS_DRIVERS_OK, etc.)
 * 
 * Returns: 0 on success, -1 on error
 */
int boot_guard_mark_stage(boot_status_t status);

/*
 * boot_guard_mark_success - Mark boot as fully successful
 * 
 * Called when system is fully booted and stable.
 * Resets failure counter.
 * 
 * Returns: 0 on success, -1 on error
 */
int boot_guard_mark_success(void);

/*
 * boot_guard_mark_failure - Mark boot as failed
 * 
 * Increments failure counter.
 * Called on kernel panic or critical error.
 * 
 * Returns: 0 on success, -1 on error
 */
int boot_guard_mark_failure(void);

/*
 * boot_guard_get_mode - Get current boot mode
 * 
 * Returns: Current boot mode flags
 */
boot_mode_flags_t boot_guard_get_mode(void);

/*
 * boot_guard_should_enter_recovery - Check if recovery mode is needed
 * 
 * Returns: 1 if recovery needed, 0 otherwise
 */
int boot_guard_should_enter_recovery(void);

/*
 * boot_guard_get_failure_count - Get consecutive failure count
 * 
 * Returns: Number of consecutive boot failures
 */
uint8_t boot_guard_get_failure_count(void);

/*
 * boot_guard_reset - Reset boot guard state
 * 
 * Only callable from recovery mode.
 * Clears failure counter and resets state.
 * 
 * Returns: 0 on success, -1 on error
 */
int boot_guard_reset(void);

/* ============ Watchdog API ============ */

/*
 * boot_guard_watchdog_start - Start the boot watchdog timer
 * 
 * If boot doesn't complete within timeout, marks as failed.
 * 
 * Returns: 0 on success, -1 on error
 */
int boot_guard_watchdog_start(void);

/*
 * boot_guard_watchdog_pet - Pet the watchdog (reset timeout)
 * 
 * Call periodically during boot to prevent timeout.
 */
void boot_guard_watchdog_pet(void);

/*
 * boot_guard_watchdog_stop - Stop the watchdog timer
 * 
 * Called when boot completes successfully.
 */
void boot_guard_watchdog_stop(void);

/* ============ Debug/Info ============ */

/*
 * boot_guard_print_status - Print boot guard status to serial
 */
void boot_guard_print_status(void);

#endif /* NEOLYX_BOOT_GUARD_H */
