#include <stdint.h>
#include <stddef.h>
#include "io.h"

// PIT ports
#define PIT_COMMAND 0x43
#define PIT_CHANNEL0 0x40

// PIT frequency (1193180 Hz)
#define PIT_FREQUENCY 1193180

// Timer variables
volatile uint64_t timer_ticks = 0; /* Accessible by process.c for sleep */
static uint32_t timer_frequency = 100; // 100 Hz default

// Initialize PIT timer
void timer_init(uint32_t frequency) {
    timer_frequency = frequency;
    
    // Calculate divisor
    uint32_t divisor = PIT_FREQUENCY / frequency;
    
    // Send command byte
    outb(PIT_COMMAND, 0x36);
    
    // Send divisor (low byte first)
    outb(PIT_CHANNEL0, divisor & 0xFF);
    outb(PIT_CHANNEL0, (divisor >> 8) & 0xFF);
    
    // Reset tick counter
    timer_ticks = 0;
}

// Timer interrupt handler - called from IRQ0
void timer_handler(void) {
    timer_ticks++;
    
    // Call scheduler tick for preemptive multitasking
    extern void scheduler_tick(void);
    scheduler_tick();
    
    // Every 100 ticks (1 second at 100Hz), update system time
    if (timer_ticks % 100 == 0) {
        // Heartbeat - kernel is alive
    }
}

// Get current tick count
uint32_t timer_get_ticks(void) {
    return timer_ticks;
}

// Sleep for specified number of ticks
void timer_sleep(uint32_t ticks) {
    uint32_t start_ticks = timer_ticks;
    while (timer_ticks - start_ticks < ticks) {
        // Wait
        __asm__ volatile("hlt");
    }
}

// Sleep for specified number of milliseconds
void timer_sleep_ms(uint32_t milliseconds) {
    uint32_t ticks = (milliseconds * timer_frequency) / 1000;
    timer_sleep(ticks);
}

// Get timer frequency
uint32_t timer_get_frequency(void) {
    return timer_frequency;
} 