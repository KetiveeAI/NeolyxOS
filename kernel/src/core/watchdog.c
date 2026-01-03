/*
 * NeolyxOS Kernel Watchdog
 * 
 * Kernel-owned passive watchdog that monitors system health.
 * Services don't pet - kernel judges reality.
 * 
 * Monitors:
 *   - Scheduler progress (tick count)
 *   - IPC activity
 *   - Resource consumption
 *   - Service responsiveness
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include <stdint.h>

extern void serial_puts(const char *s);
extern uint64_t timer_get_ticks(void);

/* Watchdog configuration */
#define WATCHDOG_TIMEOUT_MS     5000    /* 5 second timeout */
#define WATCHDOG_CHECK_INTERVAL 100     /* Check every 100ms */
#define MAX_WATCHED_SERVICES    16

/* Service watch entry */
typedef struct {
    char name[32];
    uint64_t last_activity_tick;
    uint32_t timeout_ms;
    int active;
    int stall_count;
} watch_entry_t;

/* Watchdog state */
static watch_entry_t g_watched[MAX_WATCHED_SERVICES];
static int g_watch_count = 0;
static uint64_t g_last_check_tick = 0;
static uint64_t g_scheduler_ticks = 0;
static int g_watchdog_enabled = 0;

/* External fault handler */
extern void fault_route(uint64_t vector, uint64_t error_code, uint64_t rip, uint64_t cs);

/* Initialize watchdog */
void watchdog_init(void) {
    serial_puts("[WATCHDOG] Initializing kernel watchdog...\n");
    
    g_watch_count = 0;
    g_last_check_tick = 0;
    g_scheduler_ticks = 0;
    g_watchdog_enabled = 0;
    
    for (int i = 0; i < MAX_WATCHED_SERVICES; i++) {
        g_watched[i].active = 0;
    }
    
    serial_puts("[WATCHDOG] Kernel-owned passive monitoring ready\n");
}

/* Enable watchdog */
void watchdog_enable(void) {
    g_watchdog_enabled = 1;
    g_last_check_tick = timer_get_ticks();
    serial_puts("[WATCHDOG] Monitoring ENABLED\n");
}

/* Disable watchdog */
void watchdog_disable(void) {
    g_watchdog_enabled = 0;
    serial_puts("[WATCHDOG] Monitoring DISABLED\n");
}

/* Register a service for monitoring */
int watchdog_register(const char *name, uint32_t timeout_ms) {
    if (!name) {
        serial_puts("[WATCHDOG] ERROR: Null service name\n");
        return -1;
    }
    if (g_watch_count >= MAX_WATCHED_SERVICES) {
        serial_puts("[WATCHDOG] ERROR: Max services reached\n");
        return -1;
    }
    
    watch_entry_t *entry = &g_watched[g_watch_count];
    
    /* Safe copy name with bounds check */
    int i = 0;
    while (name[i] && i < 31) {
        entry->name[i] = name[i];
        i++;
    }
    entry->name[i] = '\0';
    
    entry->timeout_ms = timeout_ms ? timeout_ms : WATCHDOG_TIMEOUT_MS;
    entry->last_activity_tick = timer_get_ticks();
    entry->active = 1;
    entry->stall_count = 0;
    
    g_watch_count++;
    
    serial_puts("[WATCHDOG] Watching: ");
    serial_puts(entry->name);  /* Use safe copy */
    serial_puts("\n");
    
    return g_watch_count - 1;
}

/* Record activity for a service (called by kernel when service does work) */
void watchdog_activity(int id) {
    if (id < 0 || id >= g_watch_count) return;
    if (!g_watched[id].active) return;
    
    g_watched[id].last_activity_tick = timer_get_ticks();
    g_watched[id].stall_count = 0;
}

/* Record scheduler tick (called from timer interrupt) */
void watchdog_scheduler_tick(void) {
    g_scheduler_ticks++;
}

/* Check for stalled services (called periodically from timer) */
void watchdog_check(void) {
    if (!g_watchdog_enabled) return;
    
    uint64_t now = timer_get_ticks();
    
    /* Only check at intervals */
    if (now - g_last_check_tick < WATCHDOG_CHECK_INTERVAL) {
        return;
    }
    g_last_check_tick = now;
    
    /* Check each watched service */
    for (int i = 0; i < g_watch_count; i++) {
        if (!g_watched[i].active) continue;
        
        uint64_t elapsed = now - g_watched[i].last_activity_tick;
        uint32_t timeout_ticks = g_watched[i].timeout_ms; /* Approx 1 tick per ms */
        
        if (elapsed > timeout_ticks) {
            g_watched[i].stall_count++;
            
            serial_puts("[WATCHDOG] STALL detected: ");
            serial_puts(g_watched[i].name);
            serial_puts(" (count: ");
            /* Safe integer to char - stall_count is bounded */
            int count = g_watched[i].stall_count;
            if (count >= 10) serial_putc('0' + (count / 10));
            serial_putc('0' + (count % 10));
            serial_puts(")\n");
            
            /* After 3 stalls, trigger fault recovery */
            if (g_watched[i].stall_count >= 3) {
                serial_puts("[WATCHDOG] Service unresponsive, triggering recovery\n");
                
                /* Mark service as zombie via fault router */
                /* 0xFF is custom vector for watchdog timeout */
                fault_route(0xFF, 0, 0, 0);
                
                /* Reset stall count to allow recovery */
                g_watched[i].stall_count = 0;
                g_watched[i].last_activity_tick = now;
            }
        }
    }
}

/* Suspend watching a service (during recovery) */
void watchdog_suspend(int id) {
    if (id < 0 || id >= g_watch_count) return;
    g_watched[id].active = 0;
    serial_puts("[WATCHDOG] Suspended: ");
    serial_puts(g_watched[id].name);
    serial_puts("\n");
}

/* Resume watching a service */
void watchdog_resume(int id) {
    if (id < 0 || id >= g_watch_count) return;
    g_watched[id].active = 1;
    g_watched[id].last_activity_tick = timer_get_ticks();
    g_watched[id].stall_count = 0;
    serial_puts("[WATCHDOG] Resumed: ");
    serial_puts(g_watched[id].name);
    serial_puts("\n");
}

/* Get scheduler tick count (for deadlock detection) */
uint64_t watchdog_get_scheduler_ticks(void) {
    return g_scheduler_ticks;
}
