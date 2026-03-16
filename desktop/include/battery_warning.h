/*
 * NeolyxOS Battery Warning Popup
 * 
 * Shows warning when battery is low.
 * Displays at 20%, 10%, and 5% thresholds.
 * 
 * Copyright (c) 2025-2026 KetiveeAI
 */

#ifndef BATTERY_WARNING_H
#define BATTERY_WARNING_H

#include <stdint.h>

/* Warning thresholds */
#define BATTERY_LOW_THRESHOLD     20
#define BATTERY_CRITICAL_THRESHOLD 10
#define BATTERY_URGENT_THRESHOLD   5

/* Warning state */
typedef struct {
    int visible;
    int battery_percent;
    int is_charging;
    uint64_t show_time;
    uint8_t opacity;
    
    int32_t x, y;
    uint32_t width, height;
    
    /* Track shown thresholds to avoid repeat warnings */
    int warned_20;
    int warned_10;
    int warned_5;
} battery_warning_state_t;

/* API */
void battery_warning_init(uint32_t screen_w, uint32_t screen_h);
void battery_warning_check(int battery_percent, int is_charging);
void battery_warning_dismiss(void);
int battery_warning_is_visible(void);
void battery_warning_tick(void);
void battery_warning_render(uint32_t *fb, uint32_t pitch);

/* Reset warning flags (e.g., when plugged in) */
void battery_warning_reset(void);

#endif /* BATTERY_WARNING_H */
