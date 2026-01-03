/*
 * NeolyxOS Boot Guard Implementation
 * 
 * Crash-proof boot sequence protection.
 * Tracks boot status, detects failures, triggers recovery.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include <stdint.h>
#include "../../include/core/boot_guard.h"

/* ============ External Dependencies ============ */

extern void serial_puts(const char *s);
extern void serial_putc(char c);
extern uint64_t pit_get_ticks(void);
extern void *kmalloc(uint64_t size);
extern void kfree(void *ptr);

/* Forward declarations for integration */
extern void recovery_mode_enter(void);

/* ============ Static State ============ */

static boot_guard_state_t g_boot_state = {0};
static volatile uint64_t g_watchdog_last_pet = 0;
static volatile int g_watchdog_enabled = 0;

/* ============ Utility Functions ============ */

static void serial_dec(uint64_t val) {
    char buf[21];
    int i = 20;
    buf[i--] = '\0';
    if (val == 0) { serial_putc('0'); return; }
    while (val > 0) { buf[i--] = '0' + (val % 10); val /= 10; }
    serial_puts(&buf[i + 1]);
}

static uint32_t simple_crc32(const void *data, uint64_t len) {
    const uint8_t *bytes = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFF;
    for (uint64_t i = 0; i < len; i++) {
        crc ^= bytes[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
        }
    }
    return ~crc;
}

/* ============ State Persistence ============ */

/* 
 * In a real implementation, this would read/write to:
 * - UEFI variables (NVRAM)
 * - Boot partition special sector
 * - RTC CMOS memory
 * 
 * For now, we use an in-memory structure that survives
 * soft reboots (if memory is preserved by bootloader).
 */

/* Magic address in high memory for boot state (preserved across soft reboot) */
#define BOOT_STATE_ADDR 0x1000  /* Just after first page */

static boot_guard_state_t *get_persistent_state(void) {
    /* In production: read from NVRAM or boot partition */
    /* For now: use fixed memory location */
    return (boot_guard_state_t *)BOOT_STATE_ADDR;
}

static int save_state(void) {
    boot_guard_state_t *persistent = get_persistent_state();
    
    /* Calculate checksum (excluding checksum field itself) */
    g_boot_state.checksum = simple_crc32(&g_boot_state, 
        sizeof(boot_guard_state_t) - sizeof(uint32_t));
    
    /* Copy to persistent memory */
    *persistent = g_boot_state;
    
    return 0;
}

static int load_state(void) {
    boot_guard_state_t *persistent = get_persistent_state();
    
    /* Check magic */
    if (persistent->magic != BOOT_GUARD_MAGIC) {
        /* First boot or corrupted - initialize fresh */
        return -1;
    }
    
    /* Verify checksum */
    uint32_t stored_checksum = persistent->checksum;
    uint32_t calc_checksum = simple_crc32(persistent, 
        sizeof(boot_guard_state_t) - sizeof(uint32_t));
    
    if (stored_checksum != calc_checksum) {
        /* Corrupted state */
        return -1;
    }
    
    /* Load state */
    g_boot_state = *persistent;
    
    return 0;
}

/* ============ Boot Guard API Implementation ============ */

int boot_guard_init(void) {
    serial_puts("[BOOT_GUARD] Initializing boot guard...\n");
    
    /* Try to load previous state */
    if (load_state() < 0) {
        /* First boot or corrupted - initialize fresh */
        serial_puts("[BOOT_GUARD] First boot or corrupted state, initializing fresh\n");
        
        g_boot_state.magic = BOOT_GUARD_MAGIC;
        g_boot_state.status = BOOT_STATUS_UNKNOWN;
        g_boot_state.mode = BOOT_MODE_NORMAL;
        g_boot_state.failure_count = 0;
        g_boot_state.last_boot_time = pit_get_ticks();
        g_boot_state.last_success_time = 0;
        g_boot_state.watchdog_timeout_ms = BOOT_GUARD_TIMEOUT_MS;
    } else {
        serial_puts("[BOOT_GUARD] Loaded previous boot state\n");
        
        /* Check if previous boot was incomplete */
        if (g_boot_state.status == BOOT_STATUS_BOOTING ||
            g_boot_state.status == BOOT_STATUS_DRIVERS_OK ||
            g_boot_state.status == BOOT_STATUS_FS_OK) {
            
            /* Previous boot didn't complete - increment failure */
            g_boot_state.failure_count++;
            serial_puts("[BOOT_GUARD] Previous boot incomplete! Failure count: ");
            serial_dec(g_boot_state.failure_count);
            serial_puts("\n");
        }
    }
    
    /* Check if we should enter recovery */
    if (g_boot_state.failure_count >= BOOT_GUARD_MAX_FAILURES) {
        serial_puts("[BOOT_GUARD] Too many failures, entering RECOVERY MODE\n");
        g_boot_state.mode = BOOT_MODE_RECOVERY;
    }
    
    /* Mark as BOOTING */
    g_boot_state.status = BOOT_STATUS_BOOTING;
    g_boot_state.last_boot_time = pit_get_ticks();
    save_state();
    
    boot_guard_print_status();
    
    return 0;
}

int boot_guard_mark_stage(boot_status_t status) {
    if (status <= g_boot_state.status && g_boot_state.status != BOOT_STATUS_BOOTING) {
        /* Can't go backwards (except from OK to BOOTING) */
        return -1;
    }
    
    g_boot_state.status = status;
    save_state();
    
    const char *stage_name = "unknown";
    switch (status) {
        case BOOT_STATUS_BOOTING:    stage_name = "BOOTING"; break;
        case BOOT_STATUS_DRIVERS_OK: stage_name = "DRIVERS_OK"; break;
        case BOOT_STATUS_FS_OK:      stage_name = "FS_OK"; break;
        case BOOT_STATUS_GUI_OK:     stage_name = "GUI_OK"; break;
        case BOOT_STATUS_OK:         stage_name = "OK"; break;
        case BOOT_STATUS_FAILED:     stage_name = "FAILED"; break;
        default: break;
    }
    
    serial_puts("[BOOT_GUARD] Stage complete: ");
    serial_puts(stage_name);
    serial_puts("\n");
    
    /* Pet watchdog on each stage */
    boot_guard_watchdog_pet();
    
    return 0;
}

int boot_guard_mark_success(void) {
    serial_puts("[BOOT_GUARD] Boot completed successfully!\n");
    
    g_boot_state.status = BOOT_STATUS_OK;
    g_boot_state.failure_count = 0;  /* Reset failure counter */
    g_boot_state.last_success_time = pit_get_ticks();
    save_state();
    
    /* Stop watchdog */
    boot_guard_watchdog_stop();
    
    return 0;
}

int boot_guard_mark_failure(void) {
    serial_puts("[BOOT_GUARD] Boot marked as FAILED!\n");
    
    g_boot_state.status = BOOT_STATUS_FAILED;
    g_boot_state.failure_count++;
    save_state();
    
    serial_puts("[BOOT_GUARD] Failure count: ");
    serial_dec(g_boot_state.failure_count);
    serial_puts("\n");
    
    return 0;
}

boot_mode_flags_t boot_guard_get_mode(void) {
    return (boot_mode_flags_t)g_boot_state.mode;
}

int boot_guard_should_enter_recovery(void) {
    return (g_boot_state.mode & BOOT_MODE_RECOVERY) != 0 ||
           g_boot_state.failure_count >= BOOT_GUARD_MAX_FAILURES;
}

uint8_t boot_guard_get_failure_count(void) {
    return g_boot_state.failure_count;
}

int boot_guard_reset(void) {
    /* Only allow reset in recovery mode */
    if (!(g_boot_state.mode & BOOT_MODE_RECOVERY)) {
        serial_puts("[BOOT_GUARD] Reset only allowed in recovery mode!\n");
        return -1;
    }
    
    serial_puts("[BOOT_GUARD] Resetting boot guard state...\n");
    
    g_boot_state.status = BOOT_STATUS_UNKNOWN;
    g_boot_state.mode = BOOT_MODE_NORMAL;
    g_boot_state.failure_count = 0;
    save_state();
    
    return 0;
}

/* ============ Watchdog Implementation ============ */

int boot_guard_watchdog_start(void) {
    serial_puts("[BOOT_GUARD] Starting boot watchdog (");
    serial_dec(BOOT_GUARD_TIMEOUT_MS / 1000);
    serial_puts("s timeout)\n");
    
    g_watchdog_last_pet = pit_get_ticks();
    g_watchdog_enabled = 1;
    
    return 0;
}

void boot_guard_watchdog_pet(void) {
    if (g_watchdog_enabled) {
        g_watchdog_last_pet = pit_get_ticks();
    }
}

void boot_guard_watchdog_stop(void) {
    g_watchdog_enabled = 0;
    serial_puts("[BOOT_GUARD] Watchdog stopped\n");
}

/*
 * boot_guard_watchdog_check - Check if watchdog has expired
 * 
 * Called from timer interrupt or main loop.
 * If watchdog expired, triggers recovery.
 * 
 * Returns: 1 if expired, 0 otherwise
 */
int boot_guard_watchdog_check(void) {
    if (!g_watchdog_enabled) {
        return 0;
    }
    
    uint64_t now = pit_get_ticks();
    uint64_t elapsed = now - g_watchdog_last_pet;
    
    if (elapsed > g_boot_state.watchdog_timeout_ms) {
        serial_puts("[BOOT_GUARD] WATCHDOG TIMEOUT! Boot took too long.\n");
        boot_guard_mark_failure();
        
        /* Trigger recovery on next boot */
        g_watchdog_enabled = 0;
        
        return 1;
    }
    
    return 0;
}

/* ============ Debug/Info ============ */

void boot_guard_print_status(void) {
    serial_puts("\n=== Boot Guard Status ===\n");
    
    serial_puts("  Magic: 0x");
    /* Print hex */
    const char hex[] = "0123456789ABCDEF";
    uint32_t m = g_boot_state.magic;
    for (int i = 28; i >= 0; i -= 4) {
        serial_putc(hex[(m >> i) & 0xF]);
    }
    serial_puts("\n");
    
    serial_puts("  Status: ");
    switch (g_boot_state.status) {
        case BOOT_STATUS_UNKNOWN:    serial_puts("UNKNOWN"); break;
        case BOOT_STATUS_BOOTING:    serial_puts("BOOTING"); break;
        case BOOT_STATUS_DRIVERS_OK: serial_puts("DRIVERS_OK"); break;
        case BOOT_STATUS_FS_OK:      serial_puts("FS_OK"); break;
        case BOOT_STATUS_GUI_OK:     serial_puts("GUI_OK"); break;
        case BOOT_STATUS_OK:         serial_puts("OK"); break;
        case BOOT_STATUS_FAILED:     serial_puts("FAILED"); break;
        default: serial_puts("?"); break;
    }
    serial_puts("\n");
    
    serial_puts("  Mode: ");
    if (g_boot_state.mode & BOOT_MODE_RECOVERY) serial_puts("RECOVERY ");
    if (g_boot_state.mode & BOOT_MODE_SAFE) serial_puts("SAFE ");
    if (g_boot_state.mode & BOOT_MODE_VERBOSE) serial_puts("VERBOSE ");
    if (g_boot_state.mode == BOOT_MODE_NORMAL) serial_puts("NORMAL");
    serial_puts("\n");
    
    serial_puts("  Failure count: ");
    serial_dec(g_boot_state.failure_count);
    serial_puts("/");
    serial_dec(BOOT_GUARD_MAX_FAILURES);
    serial_puts("\n");
    
    serial_puts("  Watchdog: ");
    serial_puts(g_watchdog_enabled ? "ENABLED" : "DISABLED");
    serial_puts("\n");
    
    serial_puts("=========================\n\n");
}
