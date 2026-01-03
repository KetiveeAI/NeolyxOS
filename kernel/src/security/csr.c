/*
 * NeolyxOS Configurable Security Restrictions (CSR)
 * 
 * Copyright (c) 2025 KetiveeAI. All rights reserved.
 * 
 * Kernel-level security configuration flags inspired by macOS SIP/CSR.
 * Controls system integrity protections that can only be modified
 * from recovery mode.
 */

#include <stdint.h>
#include <stdbool.h>

/* Forward declarations */
extern void serial_puts(const char *s);
extern void serial_putc(char c);
extern uint64_t pit_get_ticks(void);

/*
 * CSR flag definitions
 */
#define CSR_ALLOW_UNTRUSTED_KEXTS       (1 << 0)
#define CSR_ALLOW_TASK_FOR_PID          (1 << 1)
#define CSR_ALLOW_KERNEL_DEBUGGER       (1 << 2)
#define CSR_ALLOW_UNRESTRICTED_FS       (1 << 3)
#define CSR_ALLOW_DEVICE_CONFIG         (1 << 4)
#define CSR_ALLOW_UNRESTRICTED_NVRAM    (1 << 5)
#define CSR_ALLOW_UNRESTRICTED_DTRACE   (1 << 6)
#define CSR_ALLOW_UNAUTHENTICATED_ROOT  (1 << 7)

#define CSR_ENABLED             0x00000000
#define CSR_DISABLED            0x000000FF

typedef enum {
    CSR_SOURCE_DEFAULT = 0,
    CSR_SOURCE_BOOT = 1,
    CSR_SOURCE_RECOVERY = 2,
    CSR_SOURCE_NVRAM = 3
} csr_source_t;

typedef struct {
    uint32_t flags;
    csr_source_t source;
    bool recovery_mode;
    uint64_t set_time;
} csr_config_t;

/* State */
static csr_config_t g_csr = {
    .flags = CSR_ENABLED,
    .source = CSR_SOURCE_DEFAULT,
    .recovery_mode = false,
    .set_time = 0
};

static bool g_initialized = false;

/*
 * Initialize CSR subsystem
 */
void csr_init(void)
{
    if (g_initialized) {
        return;
    }
    
    /* Default: all protections enabled (flags = 0) */
    g_csr.flags = CSR_ENABLED;
    g_csr.source = CSR_SOURCE_DEFAULT;
    g_csr.recovery_mode = false;
    g_csr.set_time = pit_get_ticks();
    
    g_initialized = true;
    
    serial_puts("[CSR] Security restrictions initialized (all enabled)\n");
}

/*
 * Check if a CSR flag is set (restriction bypassed)
 */
bool csr_check(uint32_t flag)
{
    if (!g_initialized) {
        csr_init();
    }
    
    return (g_csr.flags & flag) != 0;
}

/*
 * Get active CSR configuration
 */
csr_config_t csr_get_config(void)
{
    if (!g_initialized) {
        csr_init();
    }
    return g_csr;
}

/*
 * Get raw flags value
 */
uint32_t csr_get_active_flags(void)
{
    if (!g_initialized) {
        csr_init();
    }
    return g_csr.flags;
}

/*
 * Set CSR configuration (only in recovery mode)
 */
int csr_set_config(uint32_t flags)
{
    if (!g_initialized) {
        csr_init();
    }
    
    /* Only allow in recovery mode */
    if (!g_csr.recovery_mode) {
        serial_puts("[CSR] Error: Cannot modify CSR outside recovery mode\n");
        return -1;
    }
    
    g_csr.flags = flags;
    g_csr.source = CSR_SOURCE_RECOVERY;
    g_csr.set_time = pit_get_ticks();
    
    serial_puts("[CSR] Configuration updated: 0x");
    /* Print hex */
    const char hex[] = "0123456789ABCDEF";
    for (int i = 7; i >= 0; i--) {
        serial_putc(hex[(flags >> (i * 4)) & 0xF]);
    }
    serial_puts("\n");
    
    return 0;
}

/*
 * Enter recovery mode (allows CSR modification)
 */
void csr_enter_recovery(void)
{
    if (!g_initialized) {
        csr_init();
    }
    
    g_csr.recovery_mode = true;
    serial_puts("[CSR] Entered recovery mode - CSR modifications allowed\n");
}

/*
 * Exit recovery mode and lock CSR
 */
void csr_exit_recovery(void)
{
    g_csr.recovery_mode = false;
    serial_puts("[CSR] Exited recovery mode - CSR locked\n");
}

/*
 * Check if in recovery mode
 */
bool csr_in_recovery(void)
{
    return g_csr.recovery_mode;
}

/*
 * Display CSR status
 */
void csr_status(void)
{
    if (!g_initialized) {
        csr_init();
    }
    
    serial_puts("\n=== Configurable Security Restrictions ===\n\n");
    
    /* Print raw flags */
    serial_puts("CSR Flags: 0x");
    const char hex[] = "0123456789ABCDEF";
    for (int i = 7; i >= 0; i--) {
        serial_putc(hex[(g_csr.flags >> (i * 4)) & 0xF]);
    }
    serial_puts("\n");
    
    if (g_csr.flags == CSR_ENABLED) {
        serial_puts("Status: FULLY PROTECTED\n");
    } else if (g_csr.flags == CSR_DISABLED) {
        serial_puts("Status: DISABLED (all restrictions off)\n");
    } else {
        serial_puts("Status: PARTIALLY ENABLED\n");
    }
    
    serial_puts("\nRestrictions:\n");
    serial_puts(g_csr.flags & CSR_ALLOW_UNTRUSTED_KEXTS ? 
                "  [BYPASSED] Kernel extensions\n" : 
                "  [ACTIVE]   Kernel extensions\n");
    serial_puts(g_csr.flags & CSR_ALLOW_TASK_FOR_PID ? 
                "  [BYPASSED] Process debugging\n" : 
                "  [ACTIVE]   Process debugging\n");
    serial_puts(g_csr.flags & CSR_ALLOW_KERNEL_DEBUGGER ? 
                "  [BYPASSED] Kernel debugger\n" : 
                "  [ACTIVE]   Kernel debugger\n");
    serial_puts(g_csr.flags & CSR_ALLOW_UNRESTRICTED_FS ? 
                "  [BYPASSED] Protected filesystem\n" : 
                "  [ACTIVE]   Protected filesystem\n");
    serial_puts(g_csr.flags & CSR_ALLOW_DEVICE_CONFIG ?
                "  [BYPASSED] Device configuration\n" :
                "  [ACTIVE]   Device configuration\n");
    serial_puts(g_csr.flags & CSR_ALLOW_UNRESTRICTED_NVRAM ?
                "  [BYPASSED] System NVRAM\n" :
                "  [ACTIVE]   System NVRAM\n");
    serial_puts(g_csr.flags & CSR_ALLOW_UNRESTRICTED_DTRACE ?
                "  [BYPASSED] DTrace access\n" :
                "  [ACTIVE]   DTrace access\n");
    serial_puts(g_csr.flags & CSR_ALLOW_UNAUTHENTICATED_ROOT ?
                "  [BYPASSED] Authenticated root\n" :
                "  [ACTIVE]   Authenticated root\n");
    
    serial_puts("\nSource: ");
    switch (g_csr.source) {
        case CSR_SOURCE_DEFAULT:
            serial_puts("Default configuration\n");
            break;
        case CSR_SOURCE_BOOT:
            serial_puts("Boot parameter\n");
            break;
        case CSR_SOURCE_RECOVERY:
            serial_puts("Recovery mode\n");
            break;
        case CSR_SOURCE_NVRAM:
            serial_puts("NVRAM\n");
            break;
    }
    
    serial_puts("Recovery Mode: ");
    serial_puts(g_csr.recovery_mode ? "ACTIVE\n" : "Locked\n");
    
    serial_puts("\n");
}
