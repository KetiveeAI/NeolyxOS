/*
 * NeolyxOS Boot Sequence Manager
 * 
 * Unified boot path that initializes all subsystems and drivers
 * in the correct order with proper dependency handling.
 * 
 * Boot Stages:
 *   1. Early init (memory, serial, framebuffer)
 *   2. Core init (interrupts, timer, scheduler)
 *   3. Hardware init (PCI, ACPI, USB, storage)
 *   4. Network init (Ethernet, WiFi, TCP/IP)
 *   5. Filesystem init (VFS, NXFS, FAT)
 *   6. Late init (audio, GPU, power)
 *   7. User init (settings, desktop/server mode)
 * 
 * Copyright (c) 2025 KetiveeAI.
 */

#ifndef NEOLYX_BOOT_H
#define NEOLYX_BOOT_H

#include <stdint.h>

/* ============ Boot Stages ============ */

typedef enum {
    BOOT_STAGE_ENTRY = 0,       /* Kernel entry point */
    BOOT_STAGE_EARLY,           /* Memory, serial */
    BOOT_STAGE_CORE,            /* Interrupts, timer */
    BOOT_STAGE_HARDWARE,        /* PCI, USB, storage */
    BOOT_STAGE_NETWORK,         /* Ethernet, WiFi, TCP/IP */
    BOOT_STAGE_FILESYSTEM,      /* VFS, NXFS */
    BOOT_STAGE_LATE,            /* Audio, GPU, power */
    BOOT_STAGE_USER,            /* Settings, desktop */
    BOOT_STAGE_COMPLETE,        /* Boot complete */
} boot_stage_t;

/* ============ Boot Flags ============ */

#define BOOT_FLAG_VERBOSE       (1 << 0)    /* Verbose boot messages */
#define BOOT_FLAG_SAFE_MODE     (1 << 1)    /* Minimal drivers */
#define BOOT_FLAG_RECOVERY      (1 << 2)    /* Boot to recovery */
#define BOOT_FLAG_INSTALLER     (1 << 3)    /* Boot to installer */
#define BOOT_FLAG_SINGLE_USER   (1 << 4)    /* Single user mode */
#define BOOT_FLAG_DEBUG         (1 << 5)    /* Debug mode */

/* ============ Driver Init Entry ============ */

typedef int (*driver_init_fn)(void);

typedef struct {
    const char *name;
    driver_init_fn init;
    driver_init_fn shutdown;
    boot_stage_t stage;
    uint32_t depends;           /* Bitmask of dependencies */
    int optional;               /* Skip if fails */
    int initialized;
} driver_entry_t;

/* ============ Boot Status ============ */

typedef struct {
    boot_stage_t current_stage;
    uint32_t flags;
    
    /* Progress */
    int total_drivers;
    int initialized_drivers;
    int failed_drivers;
    
    /* Timing (ms) */
    uint32_t stage_times[BOOT_STAGE_COMPLETE + 1];
    uint32_t total_time;
    
    /* Errors */
    const char *last_error;
    const char *failed_driver;
} boot_status_t;

/* ============ Boot API ============ */

/**
 * Main boot sequence entry point.
 * Called from kernel_main after initial setup.
 */
int boot_init(uint32_t flags);

/**
 * Get current boot status.
 */
boot_status_t *boot_get_status(void);

/**
 * Get current boot stage.
 */
boot_stage_t boot_get_stage(void);

/**
 * Set boot progress callback.
 */
typedef void (*boot_progress_fn)(boot_stage_t stage, const char *driver, int percent);
void boot_set_progress_callback(boot_progress_fn callback);

/**
 * Register additional driver for boot.
 */
int boot_register_driver(driver_entry_t *driver);

/**
 * Shutdown all drivers in reverse order.
 */
int boot_shutdown(void);

/* ============ Individual Init Functions ============ */

/* Early init (Stage 1) */
int boot_init_memory(void);
int boot_init_serial(void);
int boot_init_framebuffer(void);

/* Core init (Stage 2) */
int boot_init_interrupts(void);
int boot_init_timer(void);
int boot_init_scheduler(void);

/* Hardware init (Stage 3) */
int boot_init_pci(void);
int boot_init_acpi(void);
int boot_init_usb(void);
int boot_init_storage(void);

/* Network init (Stage 4) */
int boot_init_network(void);
int boot_init_ethernet(void);
int boot_init_wifi(void);
int boot_init_bluetooth(void);
int boot_init_tcpip(void);

/* Filesystem init (Stage 5) */
int boot_init_vfs(void);
int boot_init_nxfs(void);
int boot_init_fat(void);

/* Late init (Stage 6) */
int boot_init_audio(void);
int boot_init_gpu(void);
int boot_init_power(void);
int boot_init_input(void);
int boot_init_print(void);

/* User init (Stage 7) */
int boot_init_settings(void);
int boot_init_desktop(void);

/* ============ Boot Complete ============ */

/**
 * Called when boot is complete.
 * Plays boot sound, shows desktop/terminal.
 */
void boot_complete(void);

/**
 * Check if boot is complete.
 */
int boot_is_complete(void);

#endif /* NEOLYX_BOOT_H */
