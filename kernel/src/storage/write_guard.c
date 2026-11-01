/*
 * NeolyxOS Storage Write Guard - see include/storage/write_guard.h.
 *
 * Copyright (c) 2025 KetiveeAI
 */

#include <stdint.h>
#include "storage/write_guard.h"

extern void serial_puts(const char *s);

static volatile int g_write_guard;
static uint64_t g_refused;

/* Log the first refusals in full, then one line per this many */
#define WG_LOG_FIRST  8u
#define WG_LOG_EVERY  100u

static void wg_dec(uint64_t v) {
    char buf[21];
    int i = 20;
    buf[i] = '\0';
    if (v == 0) buf[--i] = '0';
    while (v > 0 && i > 0) {
        buf[--i] = (char)('0' + (v % 10));
        v /= 10;
    }
    serial_puts(&buf[i]);
}

void storage_write_guard_enable(void) {
    if (g_write_guard) return;
    g_write_guard = 1;
    serial_puts("[STORAGE] Write protection ON: every disk write will be refused until restart\n");
}

int storage_write_guard_active(void) {
    return g_write_guard;
}

int storage_write_allowed(const char *driver, uint64_t lba, uint32_t count) {
    if (!g_write_guard) return 1;

    g_refused++;
    if (g_refused <= WG_LOG_FIRST || (g_refused % WG_LOG_EVERY) == 0) {
        serial_puts("[STORAGE] Write refused (write protection): ");
        serial_puts(driver ? driver : "disk");
        serial_puts(" LBA ");
        wg_dec(lba);
        serial_puts(" count ");
        wg_dec(count);
        serial_puts(" (refusal #");
        wg_dec(g_refused);
        serial_puts(")\n");
    }
    return 0;
}
