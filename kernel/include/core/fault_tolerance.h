/*
 * NeolyxOS Fault Tolerance Subsystem Header
 * 
 * Provides:
 *   - Fault routing and classification
 *   - Emergency stack for corrupted stack recovery
 *   - Reboot guard (no software reboots)
 *   - Kernel watchdog (passive monitoring)
 *   - Service registry
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef FAULT_TOLERANCE_H
#define FAULT_TOLERANCE_H

#include <stdint.h>

/* ============ Fault Router ============ */

typedef enum {
    FAULT_ORIGIN_USER,
    FAULT_ORIGIN_SERVICE,
    FAULT_ORIGIN_DRIVER,
    FAULT_ORIGIN_KERNEL
} fault_origin_t;

typedef enum {
    SVC_STATE_UNLOADED,
    SVC_STATE_STARTING,
    SVC_STATE_RUNNING,
    SVC_STATE_ZOMBIE,
    SVC_STATE_RECOVERING,
    SVC_STATE_DEGRADED,
    SVC_STATE_FAILED
} svc_state_t;

void fault_router_init(void);
void fault_route(uint64_t vector, uint64_t error_code, uint64_t rip, uint64_t cs);
int svc_register(const char *name);
void svc_set_running(int id);
void svc_set_recovered(int id);
int svc_recovery_pending(char *out_name, int max_len);
svc_state_t svc_get_state(int id);
int is_safe_halt_mode(void);

/* ============ Emergency Stack ============ */

void emergency_stack_init(void);
uint64_t emergency_stack_get_rsp(void);
uint64_t emergency_stack_get_base(void);
uint64_t emergency_stack_get_size(void);
void emergency_stack_enter(void);
int emergency_stack_active(void);

/* ============ Reboot Guard ============ */

void reboot_guard_init(void);
int reboot_request(const char *reason, int is_hardware_watchdog);
void reboot_guard_check_last_boot(void);
int reboot_guard_get_block_count(void);
void reboot_guard_set_reason(const char *reason);
void nx_reset(void);
void nx_triple_fault_handler(void);

/* ============ Watchdog ============ */

void watchdog_init(void);
void watchdog_enable(void);
void watchdog_disable(void);
int watchdog_register(const char *name, uint32_t timeout_ms);
void watchdog_activity(int id);
void watchdog_scheduler_tick(void);
void watchdog_check(void);
void watchdog_suspend(int id);
void watchdog_resume(int id);
uint64_t watchdog_get_scheduler_ticks(void);

/* ============ Convenience Macro ============ */

/* Initialize all fault tolerance subsystems */
#define FAULT_TOLERANCE_INIT() do { \
    emergency_stack_init(); \
    fault_router_init(); \
    reboot_guard_init(); \
    watchdog_init(); \
} while(0)

#endif /* FAULT_TOLERANCE_H */
