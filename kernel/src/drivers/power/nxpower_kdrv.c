/*
 * NXPower Kernel Driver Implementation
 * 
 * Copyright (c) 2025 KetiveeAI - KETIVEEAI License
 */

#include "drivers/nxpower_kdrv.h"
#include <stddef.h>

extern void serial_puts(const char *s);
extern void serial_putc(char c);

static struct {
    int initialized;
    nxpower_status_t status;
    uint8_t charge_limit;
} g_power = {0};

static void serial_dec(uint32_t val) {
    char buf[12];
    int i = 0;
    if (val == 0) { serial_putc('0'); return; }
    while (val > 0) { buf[i++] = '0' + (val % 10); val /= 10; }
    while (i > 0) serial_putc(buf[--i]);
}

int nxpower_kdrv_init(void) {
    if (g_power.initialized) return 0;
    
    serial_puts("[NXPower] Initializing v" NXPOWER_KDRV_VERSION "\n");
    
    /* Default status (AC power) */
    g_power.status.percent = 100;
    g_power.status.health = 100;
    g_power.status.temperature = 25;
    g_power.status.voltage_mv = 12000;
    g_power.status.current_ma = 0;
    g_power.status.capacity_mah = 0;
    g_power.status.charge_state = NXCHARGE_NONE;
    g_power.status.power_source = NXPOWER_AC;
    g_power.charge_limit = 100;
    
    /* TODO: Probe ACPI/EC for battery info */
    
    g_power.initialized = 1;
    serial_puts("[NXPower] Ready\n");
    return 0;
}

void nxpower_kdrv_shutdown(void) {
    if (!g_power.initialized) return;
    g_power.initialized = 0;
}

int nxpower_kdrv_get_status(nxpower_status_t *status) {
    if (!g_power.initialized || !status) return -1;
    *status = g_power.status;
    return 0;
}

int nxpower_kdrv_set_charge_limit(uint8_t percent) {
    if (!g_power.initialized) return -1;
    g_power.charge_limit = percent > 100 ? 100 : percent;
    return 0;
}

void nxpower_kdrv_debug(void) {
    serial_puts("\n=== NXPower Debug ===\n");
    serial_puts("Battery: ");
    serial_dec(g_power.status.percent);
    serial_puts("%\nSource: ");
    serial_puts(g_power.status.power_source == NXPOWER_AC ? "AC" : 
                g_power.status.power_source == NXPOWER_BATTERY ? "Battery" : "USB");
    serial_puts("\n=====================\n\n");
}
