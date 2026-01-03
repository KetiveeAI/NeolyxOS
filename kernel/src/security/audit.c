/*
 * NeolyxOS Audit Implementation
 * 
 * Security event auditing and logging.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include "security/audit.h"

/* External dependencies */
extern void serial_puts(const char *s);
extern void serial_putc(char c);
extern uint64_t pit_get_ticks(void);

#define MAX_AUDIT_LOGS 128

typedef struct {
    uint64_t timestamp;
    uint32_t type;
    uint32_t result;
    char message[64];
    uint32_t uid;
} audit_entry_t;

static audit_entry_t audit_log_buf[MAX_AUDIT_LOGS];
static int audit_head = 0;
static int audit_count = 0;

static void str_copy(char *dst, const char *src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

void audit_init(void) {
    serial_puts("[AUDIT] Initializing audit subsystem...\n");
    audit_head = 0;
    audit_count = 0;
}

void audit_log(uint32_t type, uint32_t result, const char *msg) {
    int slot = audit_head;
    
    audit_log_buf[slot].timestamp = pit_get_ticks();
    audit_log_buf[slot].type = type;
    audit_log_buf[slot].result = result;
    str_copy(audit_log_buf[slot].message, msg, 64);
    
    // Circular buffer
    audit_head = (audit_head + 1) % MAX_AUDIT_LOGS;
    if (audit_count < MAX_AUDIT_LOGS) audit_count++;
    
    // Also print to serial for immediate visual
    if (result == AUDIT_FAILURE) {
        serial_puts("[AUDIT] ALERT: ");
    } else {
        serial_puts("[AUDIT] INFO: ");
    }
    serial_puts(msg);
    serial_puts("\n");
}

void audit_log_user(uint32_t type, uint32_t result, const char *user, const char *msg) {
    /* For simplicity, we just use the generic log for now, maybe prefix user */
    /* Real impl would store UID differently */
    (void)user;
    audit_log(type, result, msg);
}

void audit_dump(void) {
    /* Print all logs (simple implementation) */
    /* Real impl might write to disk or network */
    serial_puts("[AUDIT] Dumping logs not implemented yet\n");
}
