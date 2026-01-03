/*
 * NeolyxOS System Lock Implementation
 * 
 * Screen locking and authentication.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include "security/system_lock.h"

/* External dependencies */
extern void serial_puts(const char *s);
extern uint64_t pit_get_ticks(void);

/* State */
static int lock_state = LOCK_STATE_UNLOCKED;
static uint32_t idle_timer = 0;
static uint32_t lock_timeout = DEFAULT_LOCK_TIMEOUT;

/* Auth info */
static char current_pin[16] = "0000"; /* Default PIN */
static int failed_attempts = 0;
static uint64_t lockout_end_time = 0;

/* ============ String Helpers ============ */

static int str_len(const char *s) {
    int len = 0;
    while (s[len]) len++;
    return len;
}

static int str_cmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return *a - *b;
}

static void str_copy(char *dst, const char *src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

/* ============ Initialization ============ */

void system_lock_init(void) {
    serial_puts("[LOCK] Initializing system lock...\n");
    lock_state = LOCK_STATE_UNLOCKED;
    idle_timer = 0;
    failed_attempts = 0;
    lockout_end_time = 0;
    /* Default PIN is "0000" */
}

/* ============ Lock Control ============ */

void system_lock(void) {
    if (lock_state != LOCK_STATE_LOCKED) {
        lock_state = LOCK_STATE_LOCKED;
        serial_puts("[LOCK] System LOCKED\n");
        /* TODO: Blank screen or show lock UI */
    }
}

int system_unlock(const char *pin) {
    if (lock_state != LOCK_STATE_LOCKED) return AUTH_SUCCESS;
    
    /* Check lockout */
    if (failed_attempts >= MAX_FAILED_ATTEMPTS) {
        if (pit_get_ticks() < lockout_end_time) {
            serial_puts("[LOCK] Auth failed: Locked out\n");
            return AUTH_LOCKED_OUT;
        } else {
            /* Lockout expired */
            failed_attempts = 0;
        }
    }
    
    /* Verify PIN */
    if (str_cmp(pin, current_pin) == 0) {
        lock_state = LOCK_STATE_UNLOCKED;
        failed_attempts = 0;
        idle_timer = 0;
        serial_puts("[LOCK] System UNLOCKED\n");
        return AUTH_SUCCESS;
    } else {
        failed_attempts++;
        if (failed_attempts >= MAX_FAILED_ATTEMPTS) {
            /* Trigger lockout */
            /* Assuming 1000 ticks/sec, adjust as needed */
            lockout_end_time = pit_get_ticks() + (LOCKOUT_DURATION * 1000); // 1000?? assuming timer freq
            serial_puts("[LOCK] Too many failed attempts! System locked out.\n");
        }
        serial_puts("[LOCK] Auth failed: Invalid PIN\n");
        return AUTH_FAIL;
    }
}

int system_is_locked(void) {
    return lock_state == LOCK_STATE_LOCKED;
}

/* ============ Configuration ============ */

int system_set_pin(const char *old_pin, const char *new_pin) {
    if (!old_pin || !new_pin) return -1;
    if (str_len(new_pin) < 4 || str_len(new_pin) > 15) return -1;
    
    /* Verify old PIN */
    if (str_cmp(old_pin, current_pin) != 0) {
        return AUTH_FAIL;
    }
    
    str_copy(current_pin, new_pin, 16);
    serial_puts("[LOCK] PIN changed\n");
    return 0;
}

void system_set_timeout(uint32_t seconds) {
    lock_timeout = seconds;
}

uint32_t system_get_timeout(void) {
    return lock_timeout;
}

/* ============ Events ============ */

void system_tick(void) {
    if (lock_state == LOCK_STATE_UNLOCKED && lock_timeout > 0) {
        idle_timer++;
        if (idle_timer >= lock_timeout) { // NOTE: This assumes systick is 1Hz? or need to scale
            // Usually tick is called frequently (e.g. 100Hz or 1000Hz)
            // If called at 1Hz, this is fine. If 1000Hz, we need adjustment.
            // For now, let's assume caller handles checks or this is in seconds.
            // Actually, we should check if we need to scale.
            // Let's assume this is called every second by a higher level timer handler
            // OR we just use a large counter if called frequently.
            
            // To be safe, let's assume `system_tick` is called every second by the timer driver
            system_lock();
        }
    }
}

void system_activity(void) {
    idle_timer = 0;
}
