/*
 * NeolyxOS PIT Timer Implementation
 * 
 * 8253/8254 programmable interval timer driver.
 * Provides system tick counter and sleep functionality.
 * 
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include "drivers/pit.h"

/* ============ External Dependencies ============ */

extern void serial_puts(const char *s);
extern void serial_putc(char c);

/* I/O port access */
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/* ============ PIT State ============ */

static volatile uint64_t pit_ticks = 0;

/* ============ PIT Implementation ============ */

void pit_init(void) {
    serial_puts("[PIT] Initializing timer...\n");
    
    /* Calculate divisor for ~1000 Hz */
    uint16_t divisor = PIT_FREQUENCY / PIT_TICK_RATE;
    
    /* Channel 0, Access mode: lobyte/hibyte, Mode 3: square wave */
    outb(PIT_COMMAND, 0x36);
    
    /* Send divisor */
    outb(PIT_CHANNEL0, divisor & 0xFF);         /* Low byte */
    outb(PIT_CHANNEL0, (divisor >> 8) & 0xFF);  /* High byte */
    
    serial_puts("[PIT] Timer running at ~1000 Hz\n");
}

static void pit_serial_dec(uint64_t val) {
    char buf[21];
    int i = 20;
    buf[i--] = '\0';
    if (val == 0) { serial_putc('0'); return; }
    while (val > 0 && i >= 0) { buf[i--] = '0' + (val % 10); val /= 10; }
    serial_puts(&buf[i + 1]);
}

void pit_tick(void) {
    pit_ticks++;
    
    /* Debug: trace first 5 ticks and every 5000th tick */
    if (pit_ticks <= 5 || pit_ticks % 5000 == 0) {
        serial_puts("[PIT] tick #");
        pit_serial_dec(pit_ticks);
        serial_puts("\n");
    }
    
    /* TODO: Re-enable when desktop has proper process structure
     * The desktop was started with IRETQ directly, not via process_create(),
     * so the scheduler has no valid context to switch.
     */
    #if 0
    /* Call scheduler tick every 10ms (every 10 ticks at 1000Hz) */
    extern void scheduler_tick(void);
    if ((pit_ticks % 10) == 0) {
        scheduler_tick();
    }
    #endif
}

uint64_t pit_get_ticks(void) {
    return pit_ticks;
}

uint64_t pit_get_uptime_ms(void) {
    return pit_ticks;  /* Each tick is ~1ms */
}

uint64_t pit_get_uptime_sec(void) {
    return pit_ticks / 1000;
}

void pit_sleep_ms(uint32_t ms) {
    uint64_t end = pit_ticks + ms;
    while (pit_ticks < end) {
        __asm__ volatile("hlt");  /* Wait for interrupt */
    }
}
