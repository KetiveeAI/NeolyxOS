/*
 * NXFingerprint Kernel Driver Implementation
 * 
 * Copyright (c) 2025 KetiveeAI - KETIVEEAI License
 */

#include "drivers/nxfp_kdrv.h"
#include <stddef.h>

extern void serial_puts(const char *s);
extern void serial_putc(char c);

static struct {
    int initialized;
    nxfp_info_t info;
    nxfp_template_t templates[NXFP_MAX_TEMPLATES];
    uint8_t template_count;
    int enrolling;
    uint32_t enroll_user_id;
    uint8_t enroll_captures;
} g_fp = {0};

static void serial_dec(uint32_t val) {
    char buf[12];
    int i = 0;
    if (val == 0) { serial_putc('0'); return; }
    while (val > 0) { buf[i++] = '0' + (val % 10); val /= 10; }
    while (i > 0) serial_putc(buf[--i]);
}

int nxfp_kdrv_init(void) {
    if (g_fp.initialized) return 0;
    
    serial_puts("[NXFingerprint] Initializing v" NXFP_KDRV_VERSION "\n");
    
    g_fp.info.type = NXFP_TYPE_CAPACITIVE;
    g_fp.info.sensor_width = 160;
    g_fp.info.sensor_height = 160;
    g_fp.info.template_count = 0;
    g_fp.info.connected = 0;
    
    const char *name = "Generic Fingerprint";
    for (int i = 0; name[i] && i < 63; i++) {
        g_fp.info.name[i] = name[i];
    }
    
    /* TODO: Probe USB/I2C for fingerprint sensors:
     * - Elan, Synaptics, Goodix, AuthenTec
     */
    
    g_fp.initialized = 1;
    serial_puts("[NXFingerprint] Ready\n");
    return 0;
}

void nxfp_kdrv_shutdown(void) {
    if (!g_fp.initialized) return;
    g_fp.initialized = 0;
    serial_puts("[NXFingerprint] Shutdown\n");
}

int nxfp_kdrv_get_info(nxfp_info_t *info) {
    if (!g_fp.initialized || !info) return -1;
    *info = g_fp.info;
    return 0;
}

int nxfp_kdrv_start_enroll(uint32_t user_id) {
    if (!g_fp.initialized) return -1;
    if (g_fp.template_count >= NXFP_MAX_TEMPLATES) return -2;
    
    g_fp.enrolling = 1;
    g_fp.enroll_user_id = user_id;
    g_fp.enroll_captures = 0;
    
    serial_puts("[NXFingerprint] Enrollment started for user ");
    serial_dec(user_id);
    serial_puts("\n");
    return 0;
}

int nxfp_kdrv_capture_enroll(void) {
    if (!g_fp.initialized || !g_fp.enrolling) return -1;
    
    /* TODO: Capture fingerprint image and extract features */
    g_fp.enroll_captures++;
    
    serial_puts("[NXFingerprint] Capture ");
    serial_dec(g_fp.enroll_captures);
    serial_puts("/3\n");
    
    return (g_fp.enroll_captures >= 3) ? 1 : 0;
}

int nxfp_kdrv_finish_enroll(uint32_t *template_id) {
    if (!g_fp.initialized || !g_fp.enrolling) return -1;
    if (g_fp.enroll_captures < 3) return -2;
    
    uint32_t id = g_fp.template_count;
    nxfp_template_t *t = &g_fp.templates[id];
    
    t->id = id;
    t->user_id = g_fp.enroll_user_id;
    t->quality = 85;
    t->created_at = 0; /* TODO: Get timestamp */
    
    g_fp.template_count++;
    g_fp.info.template_count = g_fp.template_count;
    g_fp.enrolling = 0;
    
    if (template_id) *template_id = id;
    
    serial_puts("[NXFingerprint] Enrolled template ");
    serial_dec(id);
    serial_puts("\n");
    return 0;
}

void nxfp_kdrv_cancel_enroll(void) {
    g_fp.enrolling = 0;
    g_fp.enroll_captures = 0;
    serial_puts("[NXFingerprint] Enrollment cancelled\n");
}

nxfp_result_t nxfp_kdrv_authenticate(uint32_t *matched_user) {
    if (!g_fp.initialized) return NXFP_RESULT_ERROR;
    if (g_fp.template_count == 0) return NXFP_RESULT_NO_MATCH;
    
    /* TODO: Capture and match against templates */
    
    if (matched_user) *matched_user = 0;
    return NXFP_RESULT_NO_FINGER;
}

int nxfp_kdrv_wait_finger(uint32_t timeout_ms) {
    (void)timeout_ms;
    /* TODO: Wait for finger on sensor */
    return 0;
}

int nxfp_kdrv_delete_template(uint32_t template_id) {
    if (!g_fp.initialized || template_id >= g_fp.template_count) return -1;
    
    /* Shift templates down */
    for (uint32_t i = template_id; i < g_fp.template_count - 1; i++) {
        g_fp.templates[i] = g_fp.templates[i + 1];
        g_fp.templates[i].id = i;
    }
    g_fp.template_count--;
    g_fp.info.template_count = g_fp.template_count;
    return 0;
}

int nxfp_kdrv_delete_user(uint32_t user_id) {
    for (int i = g_fp.template_count - 1; i >= 0; i--) {
        if (g_fp.templates[i].user_id == user_id) {
            nxfp_kdrv_delete_template(i);
        }
    }
    return 0;
}

void nxfp_kdrv_delete_all(void) {
    g_fp.template_count = 0;
    g_fp.info.template_count = 0;
    serial_puts("[NXFingerprint] All templates deleted\n");
}

void nxfp_kdrv_debug(void) {
    serial_puts("\n=== NXFingerprint Debug ===\n");
    serial_puts("Type: ");
    serial_puts(g_fp.info.type == NXFP_TYPE_CAPACITIVE ? "Capacitive" :
                g_fp.info.type == NXFP_TYPE_OPTICAL ? "Optical" : "Unknown");
    serial_puts("\nSensor: ");
    serial_dec(g_fp.info.sensor_width);
    serial_putc('x');
    serial_dec(g_fp.info.sensor_height);
    serial_puts("\nTemplates: ");
    serial_dec(g_fp.template_count);
    serial_puts("/");
    serial_dec(NXFP_MAX_TEMPLATES);
    serial_puts("\n===========================\n\n");
}
