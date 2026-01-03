/*
 * NeolyxOS Graceful Shutdown Header
 * 
 * Copyright (c) 2025 KetiveeAI. All rights reserved.
 */

#ifndef _CORE_GRACEFUL_SHUTDOWN_H
#define _CORE_GRACEFUL_SHUTDOWN_H

#include <stdint.h>
#include <stdbool.h>

/* Shutdown phases */
typedef enum {
    SHUTDOWN_PHASE_INIT = 0,
    SHUTDOWN_PHASE_NOTIFY,
    SHUTDOWN_PHASE_TERM,
    SHUTDOWN_PHASE_KILL,
    SHUTDOWN_PHASE_FILESYSTEMS,
    SHUTDOWN_PHASE_NETWORK,
    SHUTDOWN_PHASE_DRIVERS,
    SHUTDOWN_PHASE_HARDWARE,
    SHUTDOWN_PHASE_COMPLETE
} shutdown_phase_t;

/* Shutdown flags */
#define SHUTDOWN_REBOOT     (1 << 0)
#define SHUTDOWN_QUICK      (1 << 1)
#define SHUTDOWN_FORCE      (1 << 2)

/* Main shutdown function */
int graceful_shutdown(uint32_t flags);

/* Convenience wrappers */
void system_power_off(void);
void system_reboot(void);
void system_quick_reboot(void);
void system_force_power_off(void);

/* Status queries */
bool system_is_shutting_down(void);
shutdown_phase_t system_shutdown_phase(void);

#endif /* _CORE_GRACEFUL_SHUTDOWN_H */
