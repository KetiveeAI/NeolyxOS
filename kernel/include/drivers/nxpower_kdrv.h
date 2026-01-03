/*
 * NXPower Kernel Driver (nxpower_kdrv)
 * 
 * Battery and power management for NeolyxOS
 * 
 * Copyright (c) 2025 KetiveeAI - KETIVEEAI License
 */

#ifndef NXPOWER_KDRV_H
#define NXPOWER_KDRV_H

#include <stdint.h>

#define NXPOWER_KDRV_VERSION "1.0.0"

typedef enum {
    NXPOWER_AC = 0,
    NXPOWER_BATTERY,
    NXPOWER_USB,
    NXPOWER_WIRELESS
} nxpower_source_t;

typedef enum {
    NXCHARGE_NONE = 0,
    NXCHARGE_CHARGING,
    NXCHARGE_DISCHARGING,
    NXCHARGE_FULL,
    NXCHARGE_NOT_CHARGING
} nxpower_charge_t;

typedef struct {
    uint8_t  percent;
    uint8_t  health;
    int16_t  temperature;
    uint32_t voltage_mv;
    int32_t  current_ma;
    uint32_t capacity_mah;
    uint32_t time_to_empty_min;
    uint32_t time_to_full_min;
    uint32_t cycle_count;
    nxpower_charge_t charge_state;
    nxpower_source_t power_source;
} nxpower_status_t;

int  nxpower_kdrv_init(void);
void nxpower_kdrv_shutdown(void);
int  nxpower_kdrv_get_status(nxpower_status_t *status);
int  nxpower_kdrv_set_charge_limit(uint8_t percent);
void nxpower_kdrv_debug(void);

#endif /* NXPOWER_KDRV_H */
