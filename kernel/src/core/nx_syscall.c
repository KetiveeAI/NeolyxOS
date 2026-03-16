/*
 * NeolyxOS Syscall Dispatcher
 * 
 * Macro-generated syscall table and dispatch logic.
 * Uses syscall_table.def as single source of truth.
 * 
 * Copyright (c) 2025 KetiveeAI. All rights reserved.
 */

#include "core/nx_abi.h"
#include "core/syscall.h"
#include "core/process.h"
#include <stdint.h>

extern void serial_puts(const char *s);
extern void serial_putc(char c);

static void serial_dec(int64_t val) {
    if (val < 0) { serial_putc('-'); val = -val; }
    char buf[21];
    int i = 20;
    buf[i--] = '\0';
    if (val == 0) { serial_putc('0'); return; }
    while (val > 0) { buf[i--] = '0' + (val % 10); val /= 10; }
    serial_puts(&buf[i + 1]);
}

/* Forward declare all syscall handlers */
#define NX_SYSCALL(num, name, handler, argc, priv, flags) \
    extern int64_t handler(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
#include "core/syscall_table.def"
#undef NX_SYSCALL

/* Generate syscall table */
#define NX_SYSCALL(num, name, handler, argc, priv, flags) \
    [num] = { handler, name, argc, priv, flags },
static const nx_syscall_desc_t nx_syscall_table[NX_SYSCALL_MAX] = {
#include "core/syscall_table.def"
};
#undef NX_SYSCALL

/* Count registered syscalls */
static int nx_syscall_count = 0;

/* ABI Query (syscall 0) */
int64_t sys_abi_query_impl(uint64_t info_ptr, uint64_t a2, uint64_t a3, 
                           uint64_t a4, uint64_t a5) {
    (void)a2; (void)a3; (void)a4; (void)a5;
    
    if (!info_ptr) return -EINVAL;
    
    nx_abi_info_t info;
    info.major = NX_ABI_MAJOR;
    info.minor = NX_ABI_MINOR;
    info.patch = NX_ABI_PATCH;
    info.flags = 0;
    info.syscall_count = nx_syscall_count;
    info.reserved[0] = 0;
    info.reserved[1] = 0;
    info.reserved[2] = 0;
    
    if (syscall_copy_to_user((void *)info_ptr, &info, sizeof(info)) != 0) {
        return -EFAULT;
    }
    
    return 0;
}

/* ABI Negotiate (syscall 1) */
int64_t sys_abi_negotiate_impl(uint64_t req_major, uint64_t req_minor,
                               uint64_t max_major, uint64_t max_minor, 
                               uint64_t a5) {
    (void)a5;
    
    /* Check if kernel ABI is within requested range */
    if (NX_ABI_MAJOR < req_major || NX_ABI_MAJOR > max_major) {
        serial_puts("[ABI] FAIL: Major version mismatch\n");
        return -EINVAL;
    }
    
    if (NX_ABI_MAJOR == req_major && NX_ABI_MINOR < req_minor) {
        serial_puts("[ABI] FAIL: Minor version too low\n");
        return -EINVAL;
    }
    
    if (NX_ABI_MAJOR == max_major && NX_ABI_MINOR > max_minor) {
        serial_puts("[ABI] FAIL: Minor version too high\n");
        return -EINVAL;
    }
    
    serial_puts("[ABI] Negotiation OK: ");
    serial_dec(NX_ABI_MAJOR);
    serial_putc('.');
    serial_dec(NX_ABI_MINOR);
    serial_putc('.');
    serial_dec(NX_ABI_PATCH);
    serial_putc('\n');
    
    return 0;
}

/* Syscall name lookup (for debugging/tracing) */
const char *nx_syscall_name(int num) {
    if (num < 0 || num >= NX_SYSCALL_MAX) return "invalid";
    if (!nx_syscall_table[num].handler) return "reserved";
    return nx_syscall_table[num].name ? nx_syscall_table[num].name : "unnamed";
}

/* Per-process filter control */
int nx_set_syscall_filter(void *proc_ptr, int mode, const uint8_t *allowed_mask) {
    process_t *proc = (process_t *)proc_ptr;
    if (!proc) return -EINVAL;
    
    proc->syscall_dispatch.filter_mode = (uint8_t)mode;
    
    if (mode == NX_DISPATCH_OFF) {
        return 0;
    }
    
    if (allowed_mask) {
        for (int i = 0; i < 32; i++) {
            proc->syscall_dispatch.allowed[i] = allowed_mask[i];
        }
    }
    
    return 0;
}

/* Check if syscall allowed for process */
int nx_syscall_allowed(void *proc_ptr, uint64_t num) {
    process_t *proc = (process_t *)proc_ptr;
    if (!proc) return 1;
    if (proc->syscall_dispatch.filter_mode == NX_DISPATCH_OFF) return 1;
    if (num >= 256) return 0;
    
    int idx = num / 8;
    int bit = num % 8;
    return (proc->syscall_dispatch.allowed[idx] >> bit) & 1;
}

/* Audit logging */
void nx_audit_log(int pid, uint64_t syscall_num, int event) {
    const char *name = nx_syscall_name((int)syscall_num);
    
    serial_puts("[AUDIT] pid=");
    serial_dec(pid);
    serial_puts(" syscall=");
    serial_puts(name);
    serial_puts(" (");
    serial_dec(syscall_num);
    serial_puts(") event=");
    serial_puts(event == NX_AUDIT_BLOCKED ? "BLOCKED" : "CALL");
    serial_putc('\n');
}

/* Branch prediction hints */
#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

/* Debug logging - only compiled if SYSCALL_DEBUG is defined */
#ifdef SYSCALL_DEBUG
#define SYSCALL_LOG(msg, num) do { serial_puts(msg); serial_dec(num); serial_putc('\n'); } while(0)
#else
#define SYSCALL_LOG(msg, num) ((void)0)
#endif

/* Main dispatch - optimized for hot path performance */
int64_t nx_syscall_dispatch(uint64_t num, uint64_t a1, uint64_t a2,
                            uint64_t a3, uint64_t a4, uint64_t a5) {
    /* Debug: trace fork syscall to find source of spurious fork */
    if (num == 7) {
        serial_puts("[SYSCALL] FORK called! num=7\n");
    }
    
    /* Fast path: range check first (most common case passes) */
    if (unlikely(num >= NX_SYSCALL_MAX)) {
        SYSCALL_LOG("[SYSCALL] Invalid: ", num);
        return -ENOSYS;
    }
    
    const nx_syscall_desc_t *sc = &nx_syscall_table[num];
    
    /* Fast path: handler exists (most syscalls do) */
    if (unlikely(!sc->handler)) {
        SYSCALL_LOG("[SYSCALL] Unimplemented: ", num);
        return -ENOSYS;
    }
    
    process_t *proc = process_current();
    
    /* Filtering check - skip if disabled (common case) */
    if (unlikely(proc && proc->syscall_dispatch.filter_mode != NX_DISPATCH_OFF)) {
        proc->syscall_dispatch.total_count++;
        
        if (!nx_syscall_allowed(proc, num)) {
            proc->syscall_dispatch.blocked_count++;
            /* Audit only for blocked calls (security important) */
            nx_audit_log(proc->pid, num, NX_AUDIT_BLOCKED);
            return -EPERM;
        }
    }
    
    /* Privilege check (rare failure case) */
    if (unlikely(sc->priv > PRIV_USER)) {
        if (!syscall_has_priv(sc->priv)) {
            return -EPERM;
        }
    }
    
    /* Audit expensive - only if flagged (very rare) */
    if (unlikely(sc->flags & NX_FLAG_AUDIT)) {
        int pid = proc ? proc->pid : 0;
        nx_audit_log(pid, num, NX_AUDIT_CALL);
    }
    
    return sc->handler(a1, a2, a3, a4, a5);
}

/* Initialize syscall subsystem */
void nx_syscall_init(void) {
    /* Count registered syscalls */
    nx_syscall_count = 0;
    for (int i = 0; i < NX_SYSCALL_MAX; i++) {
        if (nx_syscall_table[i].handler) {
            nx_syscall_count++;
        }
    }
    
    serial_puts("[SYSCALL] NeolyxOS ABI v");
    serial_dec(NX_ABI_MAJOR);
    serial_putc('.');
    serial_dec(NX_ABI_MINOR);
    serial_putc('.');
    serial_dec(NX_ABI_PATCH);
    serial_puts(" - ");
    serial_dec(nx_syscall_count);
    serial_puts(" syscalls registered\n");
}
