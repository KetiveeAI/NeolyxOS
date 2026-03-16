/*
 * NeolyxOS Health Monitor Service
 * 
 * Userspace health monitoring daemon that:
 * - Monitors system resource usage (CPU, RAM, disk)
 * - Tracks process health and crash frequency
 * - Communicates with kernel watchdog
 * - Triggers recovery actions when needed
 * 
 * Copyright (c) 2025-2026 KetiveeAI
 */

#include <stdint.h>
#include <stddef.h>
#include "../include/health_monitor.h"
#include "../include/nxsyscall.h"

/* ============ Static State ============ */

static health_state_t g_health = {0};
static int g_health_initialized = 0;

/* Thresholds for warnings/critical */
#define CPU_WARN_THRESHOLD      80
#define CPU_CRITICAL_THRESHOLD  95
#define RAM_WARN_THRESHOLD      85
#define RAM_CRITICAL_THRESHOLD  95
#define DISK_WARN_THRESHOLD     90
#define DISK_CRITICAL_THRESHOLD 98

/* Check intervals */
#define HEALTH_CHECK_INTERVAL_MS    5000    /* 5 seconds */
#define HEARTBEAT_INTERVAL_MS       1000    /* 1 second */

/* ============ Helpers ============ */

static void health_strcpy(char *dst, const char *src, int max) {
    int i = 0;
    while (src && src[i] && i < max - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

/* ============ Initialization ============ */

void health_monitor_init(void) {
    if (g_health_initialized) return;
    
    nx_debug_print("[HEALTH] Initializing health monitor service...\n");
    
    g_health.running = 0;
    g_health.cpu_usage = 0;
    g_health.ram_usage = 0;
    g_health.ram_total = 0;
    g_health.ram_used = 0;
    g_health.disk_usage = 0;
    g_health.process_count = 0;
    g_health.uptime_seconds = 0;
    g_health.crash_count = 0;
    g_health.last_check = 0;
    g_health.status = HEALTH_OK;
    health_strcpy(g_health.status_message, "System healthy", 64);
    
    g_health_initialized = 1;
    nx_debug_print("[HEALTH] Health monitor ready\n");
}

/* ============ Resource Monitoring ============ */

static void update_cpu_usage(void) {
    /* Get CPU usage from kernel sysinfo syscall */
    /* For now, simulate based on process count */
    uint32_t procs = g_health.process_count;
    if (procs < 5) g_health.cpu_usage = 5 + procs * 2;
    else if (procs < 10) g_health.cpu_usage = 15 + procs * 3;
    else g_health.cpu_usage = 30 + procs * 2;
    
    if (g_health.cpu_usage > 100) g_health.cpu_usage = 100;
}

static void update_ram_usage(void) {
    /* Query RAM stats from kernel */
    /* Use SYS_SYSINFO (syscall 65) to get memory info */
    
    /* For now, use reasonable defaults */
    g_health.ram_total = 512 * 1024 * 1024;  /* 512MB */
    g_health.ram_used = 128 * 1024 * 1024;   /* 128MB used */
    g_health.ram_usage = (g_health.ram_used * 100) / g_health.ram_total;
}

static void update_disk_usage(void) {
    /* Query disk stats */
    g_health.disk_usage = 25;  /* 25% used */
}

static void update_process_count(void) {
    /* Count active processes */
    /* For now, estimate based on desktop state */
    g_health.process_count = 5;  /* desktop + services */
}

static void update_uptime(void) {
    uint64_t ticks = nx_gettime();
    g_health.uptime_seconds = ticks / 1000;
}

/* ============ Health Assessment ============ */

static void assess_health(void) {
    health_status_t new_status = HEALTH_OK;
    char msg[64] = "System healthy";
    
    /* Check CPU */
    if (g_health.cpu_usage >= CPU_CRITICAL_THRESHOLD) {
        new_status = HEALTH_CRITICAL;
        health_strcpy(msg, "CPU overload critical!", 64);
    } else if (g_health.cpu_usage >= CPU_WARN_THRESHOLD) {
        if (new_status < HEALTH_WARNING) new_status = HEALTH_WARNING;
        if (new_status == HEALTH_WARNING) health_strcpy(msg, "CPU usage high", 64);
    }
    
    /* Check RAM */
    if (g_health.ram_usage >= RAM_CRITICAL_THRESHOLD) {
        new_status = HEALTH_CRITICAL;
        health_strcpy(msg, "Memory critical - OOM imminent!", 64);
    } else if (g_health.ram_usage >= RAM_WARN_THRESHOLD) {
        if (new_status < HEALTH_WARNING) new_status = HEALTH_WARNING;
        if (new_status == HEALTH_WARNING) health_strcpy(msg, "Memory usage high", 64);
    }
    
    /* Check disk */
    if (g_health.disk_usage >= DISK_CRITICAL_THRESHOLD) {
        new_status = HEALTH_CRITICAL;
        health_strcpy(msg, "Disk space critical!", 64);
    } else if (g_health.disk_usage >= DISK_WARN_THRESHOLD) {
        if (new_status < HEALTH_WARNING) new_status = HEALTH_WARNING;
        if (new_status == HEALTH_WARNING) health_strcpy(msg, "Disk space low", 64);
    }
    
    /* Check crash frequency */
    if (g_health.crash_count >= 3) {
        if (new_status < HEALTH_WARNING) new_status = HEALTH_WARNING;
        health_strcpy(msg, "Multiple crashes detected", 64);
    }
    
    g_health.status = new_status;
    health_strcpy(g_health.status_message, msg, 64);
    
    /* Log status change */
    if (new_status == HEALTH_CRITICAL) {
        nx_debug_print("[HEALTH] CRITICAL: ");
        nx_debug_print(msg);
        nx_debug_print("\n");
    } else if (new_status == HEALTH_WARNING) {
        nx_debug_print("[HEALTH] WARNING: ");
        nx_debug_print(msg);
        nx_debug_print("\n");
    }
}

/* ============ Main Check ============ */

void health_monitor_check(void) {
    if (!g_health_initialized) return;
    
    uint64_t now = nx_gettime();
    
    /* Only check at intervals */
    if (now - g_health.last_check < HEALTH_CHECK_INTERVAL_MS) {
        return;
    }
    g_health.last_check = now;
    
    /* Update all metrics */
    update_cpu_usage();
    update_ram_usage();
    update_disk_usage();
    update_process_count();
    update_uptime();
    
    /* Assess overall health */
    assess_health();
}

/* ============ Heartbeat ============ */

void health_monitor_heartbeat(void) {
    /* Send heartbeat to kernel watchdog */
    /* This tells the kernel the userspace is alive */
    /* TODO: Use dedicated heartbeat syscall when available */
}

/* ============ Crash Recording ============ */

void health_monitor_record_crash(const char *process_name, int exit_code) {
    g_health.crash_count++;
    
    nx_debug_print("[HEALTH] Crash recorded: ");
    if (process_name) nx_debug_print(process_name);
    nx_debug_print("\n");
    
    /* Trigger re-assessment */
    assess_health();
}

/* ============ State Access ============ */

health_state_t health_monitor_get_state(void) {
    return g_health;
}

int health_monitor_get_cpu_usage(void) {
    return g_health.cpu_usage;
}

int health_monitor_get_ram_usage(void) {
    return g_health.ram_usage;
}

uint64_t health_monitor_get_ram_total(void) {
    return g_health.ram_total;
}

uint64_t health_monitor_get_ram_used(void) {
    return g_health.ram_used;
}

int health_monitor_get_status(void) {
    return g_health.status;
}

const char* health_monitor_get_status_message(void) {
    return g_health.status_message;
}

uint32_t health_monitor_get_uptime(void) {
    return g_health.uptime_seconds;
}
