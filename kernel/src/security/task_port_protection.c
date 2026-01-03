/*
 * NeolyxOS Task Port Protection
 * 
 * Copyright (c) 2025 KetiveeAI. All rights reserved.
 * 
 * Process hardening to prevent unauthorized debugging,
 * code injection, and memory inspection.
 */

#include <stdint.h>
#include <stdbool.h>

/* Forward declarations */
extern void serial_puts(const char *s);
extern void serial_putc(char c);
extern uint64_t pit_get_ticks(void);
extern void *memset(void *s, int c, size_t n);

/*
 * Protection levels
 */
typedef enum {
    TASK_PROT_STANDARD = 0,
    TASK_PROT_HARDENED = 1,
    TASK_PROT_SYSTEM = 2
} task_protection_t;

typedef enum {
    TASK_INSPECT_MEMORY = 0,
    TASK_INSPECT_REGISTERS = 1,
    TASK_INSPECT_ATTACH = 2,
    TASK_INSPECT_INJECT = 3,
    TASK_INSPECT_SIGNAL = 4
} task_inspect_type_t;

typedef enum {
    TASK_INSPECT_ALLOW = 0,
    TASK_INSPECT_DENY = 1,
    TASK_INSPECT_AUDIT = 2
} task_inspect_result_t;

#define TASK_FLAG_HARDENED_RUNTIME  (1 << 0)
#define TASK_FLAG_LIBRARY_VALIDATION (1 << 1)
#define TASK_FLAG_SANDBOX_ENFORCED  (1 << 2)
#define TASK_FLAG_ALLOW_DYLD_ENV    (1 << 3)

/*
 * Per-process protection entry
 */
typedef struct {
    uint32_t pid;
    task_protection_t protection;
    uint32_t flags;
    bool in_use;
} task_port_entry_t;

/*
 * Access grant entry
 */
typedef struct {
    uint32_t inspector_pid;
    uint32_t target_pid;
    bool in_use;
} task_access_entry_t;

#define MAX_TASK_ENTRIES 128
#define MAX_ACCESS_GRANTS 64

/* State */
static task_port_entry_t g_task_db[MAX_TASK_ENTRIES];
static task_access_entry_t g_access_grants[MAX_ACCESS_GRANTS];
static bool g_initialized = false;
static uint32_t g_deny_count = 0;
static uint32_t g_audit_count = 0;

/* Check if CSR allows task-for-pid */
__attribute__((weak))
bool csr_check(uint32_t flag) { (void)flag; return false; }

#define CSR_ALLOW_TASK_FOR_PID (1 << 1)

/*
 * Initialize task port protection
 */
void task_port_init(void)
{
    if (g_initialized) {
        return;
    }
    
    memset(g_task_db, 0, sizeof(g_task_db));
    memset(g_access_grants, 0, sizeof(g_access_grants));
    
    g_deny_count = 0;
    g_audit_count = 0;
    g_initialized = true;
    
    serial_puts("[TASK_PORT] Protection subsystem initialized\n");
}

/*
 * Find task entry
 */
static task_port_entry_t *find_task(uint32_t pid)
{
    for (int i = 0; i < MAX_TASK_ENTRIES; i++) {
        if (g_task_db[i].in_use && g_task_db[i].pid == pid) {
            return &g_task_db[i];
        }
    }
    return NULL;
}

/*
 * Set protection level for a process
 */
int task_port_set_protection(uint32_t pid, task_protection_t level)
{
    if (!g_initialized) {
        task_port_init();
    }
    
    task_port_entry_t *entry = find_task(pid);
    
    if (entry != NULL) {
        entry->protection = level;
        return 0;
    }
    
    /* Find empty slot */
    for (int i = 0; i < MAX_TASK_ENTRIES; i++) {
        if (!g_task_db[i].in_use) {
            g_task_db[i].pid = pid;
            g_task_db[i].protection = level;
            g_task_db[i].flags = 0;
            g_task_db[i].in_use = true;
            return 0;
        }
    }
    
    serial_puts("[TASK_PORT] Error: Task database full\n");
    return -1;
}

/*
 * Get protection level for a process
 */
task_protection_t task_port_get_protection(uint32_t pid)
{
    task_port_entry_t *entry = find_task(pid);
    if (entry == NULL) {
        return TASK_PROT_STANDARD;
    }
    return entry->protection;
}

/*
 * Check if access is granted
 */
static bool has_access_grant(uint32_t inspector_pid, uint32_t target_pid)
{
    for (int i = 0; i < MAX_ACCESS_GRANTS; i++) {
        if (g_access_grants[i].in_use &&
            g_access_grants[i].inspector_pid == inspector_pid &&
            g_access_grants[i].target_pid == target_pid) {
            return true;
        }
    }
    return false;
}

static void print_pid(uint32_t pid)
{
    char buf[16];
    int i = 0;
    
    if (pid == 0) {
        serial_putc('0');
        return;
    }
    
    while (pid > 0) {
        buf[i++] = '0' + (pid % 10);
        pid /= 10;
    }
    
    while (i > 0) {
        serial_putc(buf[--i]);
    }
}

/*
 * Check if inspection is allowed
 */
task_inspect_result_t task_port_check_inspection(
    uint32_t inspector_pid,
    uint32_t target_pid,
    task_inspect_type_t type
) {
    if (!g_initialized) {
        task_port_init();
    }
    
    /* Self-inspection always allowed */
    if (inspector_pid == target_pid) {
        return TASK_INSPECT_ALLOW;
    }
    
    /* CSR override */
    if (csr_check(CSR_ALLOW_TASK_FOR_PID)) {
        g_audit_count++;
        return TASK_INSPECT_AUDIT;
    }
    
    /* Check explicit grants */
    if (has_access_grant(inspector_pid, target_pid)) {
        return TASK_INSPECT_ALLOW;
    }
    
    /* Get target protection level */
    task_protection_t prot = task_port_get_protection(target_pid);
    
    switch (prot) {
        case TASK_PROT_SYSTEM:
            /* System processes cannot be inspected */
            serial_puts("[TASK_PORT] Denied inspection of system process ");
            print_pid(target_pid);
            serial_puts(" by ");
            print_pid(inspector_pid);
            serial_puts("\n");
            g_deny_count++;
            return TASK_INSPECT_DENY;
            
        case TASK_PROT_HARDENED:
            /* Hardened processes: only allow signals */
            if (type == TASK_INSPECT_SIGNAL) {
                g_audit_count++;
                return TASK_INSPECT_AUDIT;
            }
            serial_puts("[TASK_PORT] Denied ");
            switch (type) {
                case TASK_INSPECT_MEMORY: serial_puts("memory access"); break;
                case TASK_INSPECT_REGISTERS: serial_puts("register access"); break;
                case TASK_INSPECT_ATTACH: serial_puts("debugger attach"); break;
                case TASK_INSPECT_INJECT: serial_puts("code injection"); break;
                default: serial_puts("inspection"); break;
            }
            serial_puts(" to hardened process ");
            print_pid(target_pid);
            serial_puts("\n");
            g_deny_count++;
            return TASK_INSPECT_DENY;
            
        case TASK_PROT_STANDARD:
        default:
            /* Standard processes can be inspected with audit */
            g_audit_count++;
            return TASK_INSPECT_AUDIT;
    }
}

/*
 * Set hardened runtime flags
 */
int task_port_set_flags(uint32_t pid, uint32_t flags)
{
    if (!g_initialized) {
        task_port_init();
    }
    
    task_port_entry_t *entry = find_task(pid);
    
    if (entry == NULL) {
        /* Create new entry */
        for (int i = 0; i < MAX_TASK_ENTRIES; i++) {
            if (!g_task_db[i].in_use) {
                g_task_db[i].pid = pid;
                g_task_db[i].protection = TASK_PROT_STANDARD;
                g_task_db[i].flags = flags;
                g_task_db[i].in_use = true;
                return 0;
            }
        }
        return -1;
    }
    
    entry->flags = flags;
    
    /* Auto-upgrade protection level */
    if (flags & TASK_FLAG_HARDENED_RUNTIME) {
        if (entry->protection < TASK_PROT_HARDENED) {
            entry->protection = TASK_PROT_HARDENED;
        }
    }
    
    return 0;
}

/*
 * Get hardened runtime flags
 */
uint32_t task_port_get_flags(uint32_t pid)
{
    task_port_entry_t *entry = find_task(pid);
    if (entry == NULL) {
        return 0;
    }
    return entry->flags;
}

/*
 * Allow specific process to inspect another
 */
int task_port_grant_access(uint32_t inspector_pid, uint32_t target_pid)
{
    if (!g_initialized) {
        task_port_init();
    }
    
    /* Check if already granted */
    if (has_access_grant(inspector_pid, target_pid)) {
        return 0;
    }
    
    for (int i = 0; i < MAX_ACCESS_GRANTS; i++) {
        if (!g_access_grants[i].in_use) {
            g_access_grants[i].inspector_pid = inspector_pid;
            g_access_grants[i].target_pid = target_pid;
            g_access_grants[i].in_use = true;
            
            serial_puts("[TASK_PORT] Access granted: ");
            print_pid(inspector_pid);
            serial_puts(" -> ");
            print_pid(target_pid);
            serial_puts("\n");
            
            return 0;
        }
    }
    
    serial_puts("[TASK_PORT] Error: Access grant table full\n");
    return -1;
}

/*
 * Revoke inspection access
 */
int task_port_revoke_access(uint32_t inspector_pid, uint32_t target_pid)
{
    for (int i = 0; i < MAX_ACCESS_GRANTS; i++) {
        if (g_access_grants[i].in_use &&
            g_access_grants[i].inspector_pid == inspector_pid &&
            g_access_grants[i].target_pid == target_pid) {
            g_access_grants[i].in_use = false;
            return 0;
        }
    }
    return -1;
}

/*
 * Check if process has task-for-pid capability
 */
bool task_port_has_capability(uint32_t pid)
{
    /* PID 0 and 1 (kernel/init) always have capability */
    if (pid == 0 || pid == 1) {
        return true;
    }
    
    /* Check CSR override */
    if (csr_check(CSR_ALLOW_TASK_FOR_PID)) {
        return true;
    }
    
    return false;
}

static void print_number(uint32_t n)
{
    char buf[16];
    int i = 0;
    
    if (n == 0) {
        serial_putc('0');
        return;
    }
    
    while (n > 0) {
        buf[i++] = '0' + (n % 10);
        n /= 10;
    }
    
    while (i > 0) {
        serial_putc(buf[--i]);
    }
}

/*
 * Display task port status
 */
void task_port_status(void)
{
    if (!g_initialized) {
        task_port_init();
    }
    
    serial_puts("\n=== Task Port Protection Status ===\n\n");
    
    serial_puts("Initialized: ");
    serial_puts(g_initialized ? "Yes\n" : "No\n");
    
    serial_puts("Inspections denied: ");
    print_number(g_deny_count);
    serial_puts("\n");
    
    serial_puts("Inspections audited: ");
    print_number(g_audit_count);
    serial_puts("\n");
    
    /* Count entries */
    int task_count = 0;
    int system_count = 0;
    int hardened_count = 0;
    
    for (int i = 0; i < MAX_TASK_ENTRIES; i++) {
        if (g_task_db[i].in_use) {
            task_count++;
            switch (g_task_db[i].protection) {
                case TASK_PROT_SYSTEM: system_count++; break;
                case TASK_PROT_HARDENED: hardened_count++; break;
                default: break;
            }
        }
    }
    
    serial_puts("\nProtected tasks: ");
    print_number(task_count);
    serial_puts(" (system: ");
    print_number(system_count);
    serial_puts(", hardened: ");
    print_number(hardened_count);
    serial_puts(")\n");
    
    /* Count access grants */
    int grant_count = 0;
    for (int i = 0; i < MAX_ACCESS_GRANTS; i++) {
        if (g_access_grants[i].in_use) {
            grant_count++;
        }
    }
    
    serial_puts("Access grants: ");
    print_number(grant_count);
    serial_puts("\n\n");
}
