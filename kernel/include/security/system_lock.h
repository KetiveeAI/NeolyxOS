/*
 * NeolyxOS System Lock Header
 * 
 * Screen locking and authentication mechanisms.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef NEOLYX_SYSTEM_LOCK_H
#define NEOLYX_SYSTEM_LOCK_H

#include <stdint.h>

/* Lock states */
#define LOCK_STATE_UNLOCKED     0
#define LOCK_STATE_LOCKED       1
#define LOCK_STATE_DISABLED     2

/* Auth result */
#define AUTH_SUCCESS            0
#define AUTH_FAIL               -1
#define AUTH_LOCKED_OUT         -2

/* Configuration defaults */
#define DEFAULT_LOCK_TIMEOUT    300     /* 5 minutes */
#define MAX_FAILED_ATTEMPTS     5
#define LOCKOUT_DURATION        60      /* 60 seconds */

/* API Functions */
void system_lock_init(void);

/* Lock control */
void system_lock(void);
int  system_unlock(const char *pin);
int  system_is_locked(void);

/* Configuration */
int  system_set_pin(const char *old_pin, const char *new_pin);
void system_set_timeout(uint32_t seconds);
uint32_t system_get_timeout(void);

/* Events */
void system_tick(void); /* Called by timer interrupt */
void system_activity(void); /* Called on input */

#endif /* NEOLYX_SYSTEM_LOCK_H */
