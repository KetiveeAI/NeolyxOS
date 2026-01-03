/*
 * NeolyxOS Graceful Shutdown
 * 
 * Copyright (c) 2025 KetiveeAI. All rights reserved.
 * 
 * Implements clean system shutdown with phased process termination,
 * filesystem sync, driver cleanup, and hardware power-off.
 */

#include <stdint.h>
#include <stdbool.h>

/* Forward declarations */
extern void serial_puts(const char *s);
extern void serial_putc(char c);
extern uint64_t pit_get_ticks(void);

/*
 * Stub implementations for shutdown dependencies
 * These provide default behavior until real implementations are hooked up
 */

/* Process management stubs */
__attribute__((weak))
int scheduler_get_process_count(void) { return 1; }

__attribute__((weak))
void scheduler_signal_all(int signal) { (void)signal; }

__attribute__((weak))
void scheduler_wait_process_exit(int timeout_ms) { (void)timeout_ms; }

__attribute__((weak))
int scheduler_force_terminate_all(void) { return 0; }

/* Filesystem stubs */
__attribute__((weak))
void vfs_sync_all(void) { }

__attribute__((weak))
void vfs_unmount_all(void) { }

__attribute__((weak))
int vfs_busy_count(void) { return 0; }

/* Network stubs */
__attribute__((weak))
void network_interfaces_down(void) { }

/* Hardware stubs - real implementations in power.c */
__attribute__((weak))
void acpi_power_off(void) { }

__attribute__((weak))
void acpi_reboot(void) { }

/* Signals */
#define SIGNAL_TERM     15
#define SIGNAL_KILL     9

/* Shutdown phases */
typedef enum {
    SHUTDOWN_PHASE_INIT = 0,
    SHUTDOWN_PHASE_NOTIFY,      /* Notify services */
    SHUTDOWN_PHASE_TERM,        /* SIGTERM to processes */
    SHUTDOWN_PHASE_KILL,        /* SIGKILL to remaining */
    SHUTDOWN_PHASE_FILESYSTEMS, /* Sync and unmount */
    SHUTDOWN_PHASE_NETWORK,     /* Bring down interfaces */
    SHUTDOWN_PHASE_DRIVERS,     /* Cleanup drivers */
    SHUTDOWN_PHASE_HARDWARE,    /* Power off/reboot */
    SHUTDOWN_PHASE_COMPLETE
} shutdown_phase_t;

/* Shutdown flags */
#define SHUTDOWN_REBOOT     (1 << 0)
#define SHUTDOWN_QUICK      (1 << 1)
#define SHUTDOWN_FORCE      (1 << 2)

/* State tracking */
static volatile bool shutdown_in_progress = false;
static volatile shutdown_phase_t current_phase = SHUTDOWN_PHASE_INIT;
static volatile uint32_t shutdown_flags = 0;

/* Timeouts (milliseconds) */
#define TIMEOUT_SIGTERM     3000
#define TIMEOUT_SIGKILL     5000
#define TIMEOUT_FS_SYNC     2000

/*
 * Print shutdown progress
 */
static void shutdown_log(const char *phase, const char *detail)
{
    serial_puts("[SHUTDOWN] ");
    serial_puts(phase);
    if (detail && detail[0]) {
        serial_puts(": ");
        serial_puts(detail);
    }
    serial_puts("\n");
}

static void shutdown_progress(int percent)
{
    serial_puts("[SHUTDOWN] Progress: ");
    if (percent < 10) serial_putc(' ');
    if (percent < 100) serial_putc(' ');
    serial_putc('0' + (percent / 100) % 10);
    serial_putc('0' + (percent / 10) % 10);
    serial_putc('0' + percent % 10);
    serial_puts("%\n");
}

/*
 * Phase 1: Notify services of impending shutdown
 */
static void phase_notify_services(void)
{
    shutdown_log("Phase 1", "Notifying services");
    current_phase = SHUTDOWN_PHASE_NOTIFY;
    
    /* TODO: Send shutdown notification to service manager */
    /* session_manager_shutdown_notify(); */
    
    shutdown_progress(10);
}

/*
 * Phase 2: Send SIGTERM to all user processes
 * Give them a chance to exit gracefully
 */
static void phase_terminate_processes(void)
{
    shutdown_log("Phase 2", "Terminating processes (SIGTERM)");
    current_phase = SHUTDOWN_PHASE_TERM;
    
    int initial_count = scheduler_get_process_count();
    shutdown_log("Active processes", "");
    
    /* Send SIGTERM to all non-system processes */
    scheduler_signal_all(SIGNAL_TERM);
    
    /* Wait for processes to exit gracefully */
    if (!(shutdown_flags & SHUTDOWN_QUICK)) {
        shutdown_log("Waiting", "up to 3 seconds for graceful exit");
        scheduler_wait_process_exit(TIMEOUT_SIGTERM);
    }
    
    int remaining = scheduler_get_process_count();
    if (remaining < initial_count) {
        shutdown_log("Processes exited", "gracefully");
    }
    
    shutdown_progress(25);
}

/*
 * Phase 3: Send SIGKILL to remaining processes
 * Force termination for stubborn processes
 */
static void phase_kill_processes(void)
{
    shutdown_log("Phase 3", "Killing remaining processes (SIGKILL)");
    current_phase = SHUTDOWN_PHASE_KILL;
    
    int remaining = scheduler_get_process_count();
    if (remaining > 1) {  /* Exclude init/kernel */
        shutdown_log("Force killing", "remaining processes");
        scheduler_signal_all(SIGNAL_KILL);
        
        if (!(shutdown_flags & SHUTDOWN_QUICK)) {
            scheduler_wait_process_exit(TIMEOUT_SIGKILL);
        }
    }
    
    /* Final forced termination if any still alive */
    remaining = scheduler_get_process_count();
    if (remaining > 1) {
        shutdown_log("Brute force", "terminating hung processes");
        scheduler_force_terminate_all();
    }
    
    shutdown_progress(40);
}

/*
 * Phase 4: Sync and unmount filesystems
 */
static void phase_filesystem_cleanup(void)
{
    shutdown_log("Phase 4", "Syncing filesystems");
    current_phase = SHUTDOWN_PHASE_FILESYSTEMS;
    
    /* Sync all dirty buffers to disk */
    vfs_sync_all();
    shutdown_log("Sync", "complete");
    
    /* Wait for busy buffers to flush */
    uint64_t start = pit_get_ticks();
    uint64_t timeout = TIMEOUT_FS_SYNC;
    
    while (vfs_busy_count() > 0) {
        uint64_t elapsed = pit_get_ticks() - start;
        if (elapsed > timeout) {
            shutdown_log("Warning", "some buffers still busy");
            break;
        }
    }
    
    shutdown_progress(55);
    
    /* Unmount all filesystems */
    shutdown_log("Unmounting", "filesystems");
    vfs_unmount_all();
    
    shutdown_progress(70);
}

/*
 * Phase 5: Bring down network interfaces
 */
static void phase_network_shutdown(void)
{
    shutdown_log("Phase 5", "Shutting down network");
    current_phase = SHUTDOWN_PHASE_NETWORK;
    
    network_interfaces_down();
    
    shutdown_progress(80);
}

/*
 * Phase 6: Cleanup drivers
 */
static void phase_driver_cleanup(void)
{
    shutdown_log("Phase 6", "Driver cleanup");
    current_phase = SHUTDOWN_PHASE_DRIVERS;
    
    /* TODO: Implement driver shutdown hooks */
    /* driver_manager_shutdown_all(); */
    
    shutdown_progress(90);
}

/*
 * Phase 7: Hardware power control
 */
static void phase_hardware_control(void)
{
    shutdown_log("Phase 7", "Hardware power control");
    current_phase = SHUTDOWN_PHASE_HARDWARE;
    
    shutdown_progress(100);
    
    if (shutdown_flags & SHUTDOWN_REBOOT) {
        shutdown_log("Rebooting", "system");
        acpi_reboot();
    } else {
        shutdown_log("Powering off", "system");
        acpi_power_off();
    }
    
    /* Should not reach here */
    current_phase = SHUTDOWN_PHASE_COMPLETE;
}

/*
 * Main graceful shutdown entry point
 * flags: SHUTDOWN_REBOOT, SHUTDOWN_QUICK, SHUTDOWN_FORCE
 */
int graceful_shutdown(uint32_t flags)
{
    /* Prevent concurrent shutdowns */
    if (shutdown_in_progress) {
        shutdown_log("Error", "Shutdown already in progress");
        return -1;
    }
    
    shutdown_in_progress = true;
    shutdown_flags = flags;
    current_phase = SHUTDOWN_PHASE_INIT;
    
    serial_puts("\n");
    serial_puts("====================================\n");
    serial_puts("    NeolyxOS System Shutdown\n");
    serial_puts("====================================\n\n");
    
    if (flags & SHUTDOWN_REBOOT) {
        shutdown_log("Mode", "Reboot");
    } else {
        shutdown_log("Mode", "Power Off");
    }
    
    if (flags & SHUTDOWN_QUICK) {
        shutdown_log("Option", "Quick shutdown (minimal delay)");
    }
    
    if (flags & SHUTDOWN_FORCE) {
        shutdown_log("Option", "Force mode (skip process termination)");
        /* Skip to filesystem phase */
        goto phase_fs;
    }
    
    /* Execute shutdown phases */
    phase_notify_services();
    phase_terminate_processes();
    phase_kill_processes();
    
phase_fs:
    phase_filesystem_cleanup();
    phase_network_shutdown();
    phase_driver_cleanup();
    phase_hardware_control();
    
    /* Should never reach here */
    shutdown_in_progress = false;
    return 0;
}

/*
 * Convenience wrappers
 */
void system_power_off(void)
{
    graceful_shutdown(0);
}

void system_reboot(void)
{
    graceful_shutdown(SHUTDOWN_REBOOT);
}

void system_quick_reboot(void)
{
    graceful_shutdown(SHUTDOWN_REBOOT | SHUTDOWN_QUICK);
}

void system_force_power_off(void)
{
    graceful_shutdown(SHUTDOWN_FORCE);
}

/*
 * Check if shutdown is in progress
 */
bool system_is_shutting_down(void)
{
    return shutdown_in_progress;
}

/*
 * Get current shutdown phase
 */
shutdown_phase_t system_shutdown_phase(void)
{
    return current_phase;
}
