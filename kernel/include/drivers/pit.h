/*
 * NeolyxOS Programmable Interval Timer (PIT)
 * 
 * 8253/8254 timer for system ticks.
 * Runs at ~1000 Hz for 1ms resolution.
 * 
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#ifndef NEOLYX_PIT_H
#define NEOLYX_PIT_H

#include <stdint.h>

/* ============ PIT Constants ============ */

#define PIT_FREQUENCY       1193182     /* Base frequency in Hz */
#define PIT_TICK_RATE       1000        /* Target ticks per second */

#define PIT_CHANNEL0        0x40
#define PIT_CHANNEL1        0x41
#define PIT_CHANNEL2        0x42
#define PIT_COMMAND         0x43

/* ============ PIT API ============ */

/**
 * Initialize the PIT for system timing.
 * Sets up ~1000 Hz tick rate.
 */
void pit_init(void);

/**
 * Get current tick count since boot.
 */
uint64_t pit_get_ticks(void);

/**
 * Get time since boot in milliseconds.
 */
uint64_t pit_get_uptime_ms(void);

/**
 * Get time since boot in seconds.
 */
uint64_t pit_get_uptime_sec(void);

/**
 * Sleep for specified milliseconds.
 * Busy-wait implementation.
 */
void pit_sleep_ms(uint32_t ms);

/**
 * Timer tick handler (called from IRQ0).
 */
void pit_tick(void);

#endif /* NEOLYX_PIT_H */
