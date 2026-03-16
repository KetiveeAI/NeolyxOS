/*
 * NeolyxOS Health Monitor Header
 * 
 * Copyright (c) 2025-2026 KetiveeAI
 */

#ifndef HEALTH_MONITOR_H
#define HEALTH_MONITOR_H

#include <stdint.h>

/* Health status levels */
typedef enum {
    HEALTH_OK = 0,
    HEALTH_WARNING = 1,
    HEALTH_CRITICAL = 2
} health_status_t;

/* Health state structure */
typedef struct {
    int running;
    int cpu_usage;          /* 0-100% */
    int ram_usage;          /* 0-100% */
    uint64_t ram_total;     /* Total RAM in bytes */
    uint64_t ram_used;      /* Used RAM in bytes */
    int disk_usage;         /* 0-100% */
    int process_count;      /* Number of active processes */
    uint32_t uptime_seconds;/* System uptime */
    int crash_count;        /* Crashes since boot */
    uint64_t last_check;    /* Last check timestamp */
    health_status_t status; /* Overall health status */
    char status_message[64];/* Human-readable status */
} health_state_t;

/* Initialize health monitor */
void health_monitor_init(void);

/* Perform health check (call periodically) */
void health_monitor_check(void);

/* Send heartbeat to kernel watchdog */
void health_monitor_heartbeat(void);

/* Record a process crash */
void health_monitor_record_crash(const char *process_name, int exit_code);

/* State accessors */
health_state_t health_monitor_get_state(void);
int health_monitor_get_cpu_usage(void);
int health_monitor_get_ram_usage(void);
uint64_t health_monitor_get_ram_total(void);
uint64_t health_monitor_get_ram_used(void);
int health_monitor_get_status(void);
const char* health_monitor_get_status_message(void);
uint32_t health_monitor_get_uptime(void);

#endif /* HEALTH_MONITOR_H */
