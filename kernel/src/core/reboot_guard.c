/*
 * NeolyxOS Reboot Guard
 * 
 * Prevents all software-initiated reboots.
 * Only hardware watchdog timeout can trigger reset.
 * 
 * Policy:
 *   - Software NEVER reboots the system
 *   - On critical failure: enter safe mode, notify user
 *   - Hardware watchdog may reset if kernel truly deadlocked
 *   - User notified on next boot of what happened
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include <stdint.h>

extern void serial_puts(const char *s);

/* Reboot attempt log */
#define MAX_REBOOT_LOG 16

typedef struct {
    uint64_t timestamp;
    uint64_t source_rip;
    char reason[32];
} reboot_attempt_t;

static reboot_attempt_t g_reboot_log[MAX_REBOOT_LOG];
static int g_reboot_log_count = 0;

/* Guard state */
static int g_reboot_guard_active = 0;

/* Last boot reason (set on startup from CMOS/persistent storage) */
static char g_last_boot_reason[64] = "Normal";

/* Log a blocked reboot attempt */
static void log_reboot_attempt(const char *reason, uint64_t rip) {
    if (!reason) reason = "(null)";  /* Null safety */
    
    if (g_reboot_log_count >= MAX_REBOOT_LOG) {
        /* Shift log */
        for (int i = 0; i < MAX_REBOOT_LOG - 1; i++) {
            g_reboot_log[i] = g_reboot_log[i + 1];
        }
        g_reboot_log_count = MAX_REBOOT_LOG - 1;
    }
    
    reboot_attempt_t *entry = &g_reboot_log[g_reboot_log_count++];
    entry->source_rip = rip;
    entry->timestamp = 0;  /* TODO: Get actual timestamp */
    
    /* Copy reason with bounds check */
    int i = 0;
    while (reason[i] && i < 31) {
        entry->reason[i] = reason[i];
        i++;
    }
    entry->reason[i] = '\0';
    
    serial_puts("[REBOOT_GUARD] BLOCKED reboot attempt: ");
    serial_puts(entry->reason);  /* Use safe copy */
    serial_puts("\n");
}

/* Initialize reboot guard */
void reboot_guard_init(void) {
    serial_puts("[REBOOT_GUARD] Initializing...\n");
    g_reboot_guard_active = 1;
    g_reboot_log_count = 0;
    serial_puts("[REBOOT_GUARD] Active - software reboots BLOCKED\n");
}

/* Attempt to reboot - BLOCKED unless hardware watchdog */
int reboot_request(const char *reason, int is_hardware_watchdog) {
    if (!g_reboot_guard_active) {
        /* Guard not active yet (early boot) */
        return 1; /* Allow */
    }
    
    if (is_hardware_watchdog) {
        /* Hardware watchdog timeout - this is the ONLY allowed reboot */
        serial_puts("[REBOOT_GUARD] Hardware watchdog timeout - allowing reset\n");
        
        /* Store reason for next boot */
        /* TODO: Write to CMOS or persistent storage */
        
        return 1; /* Allow */
    }
    
    /* Get caller RIP */
    uint64_t rip;
    __asm__ volatile("lea (%%rip), %0" : "=r"(rip));
    
    /* Block and log */
    log_reboot_attempt(reason, rip);
    
    serial_puts("[REBOOT_GUARD] Reboot DENIED. Reason: ");
    serial_puts(reason);
    serial_puts("\n");
    serial_puts("[REBOOT_GUARD] System entering safe mode instead.\n");
    
    return 0; /* Deny */
}

/* Called at boot to check last boot reason */
void reboot_guard_check_last_boot(void) {
    /* TODO: Read from CMOS or persistent storage */
    serial_puts("[REBOOT_GUARD] Last boot reason: ");
    serial_puts(g_last_boot_reason);
    serial_puts("\n");
}

/* Get number of blocked reboot attempts */
int reboot_guard_get_block_count(void) {
    return g_reboot_log_count;
}

/* Set last boot reason (called from watchdog on fatal timeout) */
void reboot_guard_set_reason(const char *reason) {
    if (!reason) reason = "(null)";  /* Null safety */
    
    int i = 0;
    while (reason[i] && i < 63) {
        g_last_boot_reason[i] = reason[i];
        i++;
    }
    g_last_boot_reason[i] = '\0';
}

/* Replacement for any kernel reset/reboot functions */
void nx_reset(void) {
    if (reboot_request("nx_reset() called", 0)) {
        /* Allowed (hardware watchdog) */
        /* Perform actual reset via keyboard controller */
        __asm__ volatile("cli");
        uint8_t out = 0;
        while (out != 0x02) {
            __asm__ volatile("inb $0x64, %0" : "=a"(out));
            out &= 0x02;
        }
        __asm__ volatile("outb %0, $0x64" : : "a"((uint8_t)0xFE));
        __asm__ volatile("hlt");
    }
    /* Blocked - do nothing, return to caller */
}

/* Triple fault prevention - catch instead of reset */
void nx_triple_fault_handler(void) {
    serial_puts("\n[REBOOT_GUARD] TRIPLE FAULT INTERCEPTED\n");
    serial_puts("[REBOOT_GUARD] Would normally reset CPU - BLOCKED\n");
    serial_puts("[REBOOT_GUARD] Entering infinite halt instead\n");
    
    reboot_guard_set_reason("Triple fault prevented");
    
    __asm__ volatile("cli");
    for (;;) {
        __asm__ volatile("hlt");
    }
}
