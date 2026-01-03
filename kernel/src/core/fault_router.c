/*
 * NeolyxOS Fault Router
 * 
 * Routes CPU exceptions by fault origin:
 *   - Service/User fault → kill process, zombie state
 *   - Driver fault → unbind, request reload
 *   - Kernel fault → safe halt mode (no recovery)
 * 
 * CRITICAL: Kernel never panics, never auto-reboots.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include <stdint.h>

extern void serial_puts(const char *s);
extern void serial_putc(char c);

/* Fault origin classification */
typedef enum {
    FAULT_ORIGIN_USER,      /* Ring 3 user process */
    FAULT_ORIGIN_SERVICE,   /* Userspace service/daemon */
    FAULT_ORIGIN_DRIVER,    /* Sandboxed driver */
    FAULT_ORIGIN_KERNEL     /* Kernel core - unrecoverable */
} fault_origin_t;

/* Fault action */
typedef enum {
    FAULT_ACTION_KILL,      /* Kill faulting process */
    FAULT_ACTION_ZOMBIE,    /* Mark service as zombie, request recovery */
    FAULT_ACTION_UNBIND,    /* Unbind driver, request reload */
    FAULT_ACTION_SAFE_HALT  /* Enter safe halt mode */
} fault_action_t;

/* Service states */
typedef enum {
    SVC_STATE_UNLOADED = 0,
    SVC_STATE_STARTING,
    SVC_STATE_RUNNING,
    SVC_STATE_ZOMBIE,       /* Failed, pending recovery */
    SVC_STATE_RECOVERING,
    SVC_STATE_DEGRADED,     /* Running with reduced functionality */
    SVC_STATE_FAILED        /* Unrecoverable, disabled */
} svc_state_t;

/* Service registry entry (lightweight, no binaries) */
typedef struct {
    char name[32];
    svc_state_t state;
    uint32_t fault_count;
    uint32_t recovery_attempts;
    uint64_t last_fault_time;
    uint32_t config_hash;   /* For corruption detection */
} svc_entry_t;

/* Maximum registered services */
#define MAX_SERVICES 32

/* Service registry */
static svc_entry_t g_services[MAX_SERVICES];
static int g_service_count = 0;

/* Recovery policy */
#define MAX_AUTO_RECOVERY 3

/* Safe halt mode flag */
static volatile int g_safe_halt_mode = 0;

/* Serial hex output */
static void serial_hex64(uint64_t val) {
    const char *hex = "0123456789ABCDEF";
    serial_puts("0x");
    for (int i = 60; i >= 0; i -= 4) {
        serial_putc(hex[(val >> i) & 0xF]);
    }
}

/* Classify fault origin from instruction pointer and CS */
static fault_origin_t classify_fault(uint64_t rip, uint64_t cs) {
    /* Ring 3 (user mode) if CS indicates CPL 3 */
    if ((cs & 3) == 3) {
        /* Check if this is a registered service */
        /* For now, treat all Ring 3 faults as services */
        return FAULT_ORIGIN_SERVICE;
    }
    
    /* Ring 0 - check if in driver code range */
    /* TODO: Maintain driver code range map */
    /* For now, assume kernel fault */
    
    if (rip >= 0x200000 && rip < 0x400000) {
        /* Driver code region (example range) */
        return FAULT_ORIGIN_DRIVER;
    }
    
    return FAULT_ORIGIN_KERNEL;
}

/* Determine action based on origin */
static fault_action_t get_fault_action(fault_origin_t origin) {
    switch (origin) {
        case FAULT_ORIGIN_USER:
            return FAULT_ACTION_KILL;
        case FAULT_ORIGIN_SERVICE:
            return FAULT_ACTION_ZOMBIE;
        case FAULT_ORIGIN_DRIVER:
            return FAULT_ACTION_UNBIND;
        case FAULT_ORIGIN_KERNEL:
        default:
            return FAULT_ACTION_SAFE_HALT;
    }
}

/* Mark service as zombie */
static void mark_service_zombie(const char *name) {
    if (!name) return;  /* Null check */
    
    for (int i = 0; i < g_service_count; i++) {
        /* Safe string compare - check both strings character by character */
        const char *a = g_services[i].name;
        const char *b = name;
        int match = 1;
        
        while (*a && *b) {
            if (*a != *b) {
                match = 0;
                break;
            }
            a++;
            b++;
        }
        /* Strings match only if both ended at same position */
        if (match && *a == *b) {
            g_services[i].state = SVC_STATE_ZOMBIE;
            g_services[i].fault_count++;
            serial_puts("[FAULT] Service marked ZOMBIE: ");
            serial_puts(g_services[i].name);
            serial_puts("\n");
            return;
        }
    }
}

/* Enter safe halt mode - kernel corrupted, preserve state */
static void enter_safe_halt(uint64_t rip, uint64_t error, const char *fault_name) {
    g_safe_halt_mode = 1;
    
    serial_puts("\n");
    serial_puts("╔═══════════════════════════════════════════════════════════╗\n");
    serial_puts("║           NEOLYXOS KERNEL SAFE HALT MODE                  ║\n");
    serial_puts("╠═══════════════════════════════════════════════════════════╣\n");
    serial_puts("║ Kernel fault detected - preserving state                  ║\n");
    serial_puts("║ System will NOT reboot automatically                      ║\n");
    serial_puts("╚═══════════════════════════════════════════════════════════╝\n");
    serial_puts("[SAFE_HALT] Fault: ");
    serial_puts(fault_name);
    serial_puts("\n[SAFE_HALT] RIP: ");
    serial_hex64(rip);
    serial_puts("\n[SAFE_HALT] Error: ");
    serial_hex64(error);
    serial_puts("\n[SAFE_HALT] System halted. Power cycle required.\n");
    
    /* Disable interrupts and halt */
    __asm__ volatile("cli");
    for (;;) {
        __asm__ volatile("hlt");
    }
}

/* Request recovery from userspace daemon (via syscall flag) */
static volatile int g_recovery_pending = 0;
static char g_recovery_service[32];

static void request_recovery(const char *service_name) {
    /* Copy service name */
    int i = 0;
    while (service_name[i] && i < 31) {
        g_recovery_service[i] = service_name[i];
        i++;
    }
    g_recovery_service[i] = '\0';
    g_recovery_pending = 1;
    
    serial_puts("[FAULT] Recovery requested for: ");
    serial_puts(g_recovery_service);
    serial_puts("\n");
}

/* Main fault handler entry point */
void fault_route(uint64_t vector, uint64_t error_code, uint64_t rip, uint64_t cs) {
    static const char *fault_names[] = {
        "Divide Error", "Debug", "NMI", "Breakpoint",
        "Overflow", "Bound Range", "Invalid Opcode", "Device Not Available",
        "Double Fault", "Coprocessor Segment", "Invalid TSS", "Segment Not Present",
        "Stack Fault", "General Protection", "Page Fault", "Reserved",
        "x87 FPU Error", "Alignment Check", "Machine Check", "SIMD Error"
    };
    
    /* Bounds check for vector - must be within fault_names array */
    const char *fault_name;
    if (vector < 20) {
        fault_name = fault_names[vector];
    } else if (vector == 0xFF) {
        fault_name = "Watchdog Timeout";
    } else {
        fault_name = "Unknown";
    }
    
    serial_puts("[FAULT] Vector ");
    /* Safe digit output - handle vectors up to 255 */
    if (vector >= 100) serial_putc('0' + ((vector / 100) % 10));
    if (vector >= 10) serial_putc('0' + ((vector / 10) % 10));
    serial_putc('0' + (vector % 10));
    serial_puts(": ");
    serial_puts(fault_name);
    serial_puts(" at RIP=");
    serial_hex64(rip);
    serial_puts("\n");
    
    /* Classify and route */
    fault_origin_t origin = classify_fault(rip, cs);
    fault_action_t action = get_fault_action(origin);
    
    switch (action) {
        case FAULT_ACTION_KILL:
            serial_puts("[FAULT] Action: Kill user process\n");
            /* TODO: Call process termination */
            break;
            
        case FAULT_ACTION_ZOMBIE:
            serial_puts("[FAULT] Action: Mark service as ZOMBIE\n");
            /* Mark first running service as zombie if no specific name available */
            for (int i = 0; i < g_service_count; i++) {
                if (g_services[i].state == SVC_STATE_RUNNING) {
                    mark_service_zombie(g_services[i].name);
                    request_recovery(g_services[i].name);
                    break;
                }
            }
            break;
            
        case FAULT_ACTION_UNBIND:
            serial_puts("[FAULT] Action: Unbind driver, request reload\n");
            /* TODO: Driver unbind logic */
            request_recovery("driver");
            break;
            
        case FAULT_ACTION_SAFE_HALT:
            enter_safe_halt(rip, error_code, fault_name);
            /* Never returns */
            break;
    }
}

/* Register a service */
int svc_register(const char *name) {
    if (!name) return -1;  /* Null check */
    if (g_service_count >= MAX_SERVICES) return -1;
    
    svc_entry_t *svc = &g_services[g_service_count];
    
    /* Safe string copy with bounds check */
    int i = 0;
    while (name[i] && i < 31) {
        svc->name[i] = name[i];
        i++;
    }
    svc->name[i] = '\0';
    
    svc->state = SVC_STATE_UNLOADED;
    svc->fault_count = 0;
    svc->recovery_attempts = 0;
    svc->last_fault_time = 0;
    svc->config_hash = 0;
    
    g_service_count++;
    
    serial_puts("[SVC] Registered: ");
    serial_puts(svc->name);  /* Use safe copy, not parameter */
    serial_puts("\n");
    
    return g_service_count - 1;
}

/* Mark service as running */
void svc_set_running(int id) {
    if (id < 0 || id >= g_service_count) return;
    g_services[id].state = SVC_STATE_RUNNING;
    serial_puts("[SVC] Running: ");
    serial_puts(g_services[id].name);
    serial_puts("\n");
}

/* Mark service recovered */
void svc_set_recovered(int id) {
    if (id < 0 || id >= g_service_count) return;
    
    svc_entry_t *svc = &g_services[id];
    svc->recovery_attempts++;
    
    if (svc->recovery_attempts >= MAX_AUTO_RECOVERY) {
        svc->state = SVC_STATE_DEGRADED;
        serial_puts("[SVC] DEGRADED (max retries): ");
    } else {
        svc->state = SVC_STATE_RUNNING;
        serial_puts("[SVC] Recovered: ");
    }
    serial_puts(svc->name);
    serial_puts("\n");
}

/* Check if recovery is pending */
int svc_recovery_pending(char *out_name, int max_len) {
    if (!g_recovery_pending) return 0;
    
    int i = 0;
    while (g_recovery_service[i] && i < max_len - 1) {
        out_name[i] = g_recovery_service[i];
        i++;
    }
    out_name[i] = '\0';
    
    g_recovery_pending = 0;
    return 1;
}

/* Get service state */
svc_state_t svc_get_state(int id) {
    if (id < 0 || id >= g_service_count) return SVC_STATE_UNLOADED;
    return g_services[id].state;
}

/* Check if in safe halt mode */
int is_safe_halt_mode(void) {
    return g_safe_halt_mode;
}

/* Initialize fault router */
void fault_router_init(void) {
    serial_puts("[FAULT_ROUTER] Initializing...\n");
    g_service_count = 0;
    g_recovery_pending = 0;
    g_safe_halt_mode = 0;
    serial_puts("[FAULT_ROUTER] Ready - kernel never panics, never reboots\n");
}

/* Set service state by ID */
void svc_set_state(int id, int state) {
    if (id < 0 || id >= g_service_count) return;
    g_services[id].state = (svc_state_t)state;
}

/* Mark service as failed */
void svc_set_failed(int id) {
    if (id < 0 || id >= g_service_count) return;
    g_services[id].state = SVC_STATE_FAILED;
    g_services[id].fault_count++;
    serial_puts("[SVC] FAILED: ");
    serial_puts(g_services[id].name);
    serial_puts("\n");
}

/* Global safe mode flag */
static volatile int g_kernel_safe_mode = 0;

/* Enter safe mode - kernel degraded, serial shell remains */
void enter_safe_mode(void) {
    g_kernel_safe_mode = 1;
    serial_puts("\n");
    serial_puts("╔═══════════════════════════════════════════════════════════╗\n");
    serial_puts("║              NEOLYXOS SAFE MODE                           ║\n");
    serial_puts("╠═══════════════════════════════════════════════════════════╣\n");
    serial_puts("║ Desktop failed to start - falling back to serial shell   ║\n");
    serial_puts("║ System is operational, GUI unavailable                    ║\n");
    serial_puts("╚═══════════════════════════════════════════════════════════╝\n");
    /* Don't halt - return to caller which falls through to serial shell */
}

/* Check if kernel is in safe mode */
int is_kernel_safe_mode(void) {
    return g_kernel_safe_mode;
}
