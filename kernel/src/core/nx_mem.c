/*
 * NeolyxOS App Classes, Memory Pressure and Memory Stats (K1, ABI 1.3)
 *
 * Syscalls: proc.set_class (153), mem.pressure_open (154),
 *           mem.pressure_read (155), mem.stats (156).
 * Public ABI: include/core/nx_mem.h.
 * Spec: docs/kernel/app-classes-and-memory-pressure.md (K1, approved by
 *       the owner 2026-11-01).
 *
 * Pressure: the PMM keeps the level (mm/pmm.c, under its lock). Each
 * pressure object remembers the change counter it last read; it is
 * READABLE while that differs from the PMM's. The PMM never calls the
 * scheduler: the timer tick (nx_mem_tick) notices a change, logs it and
 * wakes obj.wait waiters, which re-check their handles (level-triggered,
 * like channels).
 *
 * Syscalls run with interrupts masked (process.c), so the process table
 * and shm list walks here are consistent without extra locking.
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include "core/nx_mem.h"
#include "core/nx_handle.h"
#include "core/process.h"
#include "core/syscall.h"
#include "mm/addrspace.h"
#include "mm/kheap.h"
#include "mm/pmm.h"

extern void serial_puts(const char *s);
/* The linked kernel heap (src/mm/heap.c); its header clashes with kheap.h */
extern void heap_stats(size_t *total_size, size_t *used_size, size_t *free_size);

#define PRESSURE_RIGHTS     (NX_RIGHT_READ | NX_RIGHT_DUPLICATE)

typedef struct {
    nx_object_t hdr;                /* must be first */
    uint64_t seen_seq;              /* pmm_pressure_seq() at the last read */
} nx_mempressure_t;

_Static_assert(PMM_PRESSURE_NORMAL == NX_MEM_NORMAL &&
               PMM_PRESSURE_WARN == NX_MEM_WARN &&
               PMM_PRESSURE_CRITICAL == NX_MEM_CRITICAL,
               "PMM pressure levels must match the NX_MEM_* ABI values");

/* ============ Logging ============ */

static void m_dec(uint64_t v) {
    char buf[21];
    int i = 20;
    buf[i] = '\0';
    if (v == 0) buf[--i] = '0';
    while (v > 0 && i > 0) { buf[--i] = (char)('0' + v % 10); v /= 10; }
    serial_puts(&buf[i]);
}

static const char *level_name(uint32_t level) {
    switch (level) {
    case NX_MEM_NORMAL:   return "NORMAL";
    case NX_MEM_WARN:     return "WARN";
    case NX_MEM_CRITICAL: return "CRITICAL";
    default:              return "?";
    }
}

static const char *class_name(uint32_t cls) {
    return (cls == NX_CLASS_BACKGROUND) ? "BACKGROUND" : "FOREGROUND";
}

/* ============ Pressure objects ============ */

static void pressure_destroy(nx_object_t *obj) {
    kfree(obj);
}

uint32_t nx_mem_pressure_signals(nx_object_t *obj) {
    if (!obj || obj->type != NX_OBJ_MEMPRESSURE) return 0;
    const nx_mempressure_t *mp = (const nx_mempressure_t *)obj;
    return (mp->seen_seq != pmm_pressure_seq()) ? NX_SIGNAL_READABLE : 0;
}

void nx_mem_tick(void) {
    if (!pmm_pressure_take_change()) return;

    pmm_stats_t st;
    pmm_get_stats(&st);
    serial_puts("[MEM] pressure ");
    serial_puts(level_name(pmm_pressure_level()));
    serial_puts(": ");
    m_dec(st.free_pages * PAGE_SIZE / (1024 * 1024));
    serial_puts(" MB free, reserve ");
    m_dec(pmm_reserve_pages() * PAGE_SIZE / (1024 * 1024));
    serial_puts(" MB\n");

    nx_ipc_wake_waiters();
}

/* ============ Syscalls ============ */

/* int proc_set_class(uint32_t pid, uint32_t cls) */
int64_t sys_proc_set_class_impl(uint64_t pid, uint64_t cls, uint64_t a3, uint64_t a4, uint64_t a5) {
    (void)a3; (void)a4; (void)a5;
    process_t *caller = process_current();
    if (!caller) return -EPERM;
    if (cls != NX_CLASS_FOREGROUND && cls != NX_CLASS_BACKGROUND) return -EINVAL;
    if (pid == 0 || pid >= MAX_PROCESSES) return (pid == 0) ? -EACCES : -ESRCH;

    process_t *target = process_get((uint32_t)pid);
    if (!target || target->state == PROC_STATE_DEAD || target->state == PROC_STATE_ZOMBIE) {
        return -ESRCH;
    }

    /* Only apps have an app class: never the desktop or kernel threads */
    if (target->flags & (PROC_FLAG_SYSTEM | PROC_FLAG_KERNEL)) {
        serial_puts("[SCHED] proc.set_class refused: PID ");
        m_dec(target->pid);
        serial_puts(" is a system process (-EACCES)\n");
        return -EACCES;
    }

    /* The desktop (system class) may set any app; an app may only send
     * itself to the background */
    int caller_system = (caller->flags & PROC_FLAG_SYSTEM) != 0;
    int self_lower = (target == caller && cls == NX_CLASS_BACKGROUND);
    if (!caller_system && !self_lower) {
        serial_puts("[SCHED] proc.set_class refused: PID ");
        m_dec(caller->pid);
        serial_puts(" may not set PID ");
        m_dec(target->pid);
        serial_puts(" to ");
        serial_puts(class_name((uint32_t)cls));
        serial_puts(" (-EACCES)\n");
        return -EACCES;
    }

    uint32_t old = process_set_class(target, (uint32_t)cls);
    if (old != (uint32_t)cls) {
        serial_puts("[SCHED] PID ");
        m_dec(target->pid);
        serial_puts(" (");
        serial_puts(target->name);
        serial_puts("): class ");
        serial_puts(class_name(old));
        serial_puts(" -> ");
        serial_puts(class_name((uint32_t)cls));
        serial_puts(" by PID ");
        m_dec(caller->pid);
        serial_puts("\n");
    }
    return 0;
}

/* nx_handle_t mem_pressure_open(uint32_t flags) */
int64_t sys_mem_pressure_open_impl(uint64_t flags, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5) {
    (void)a2; (void)a3; (void)a4; (void)a5;
    process_t *proc = process_current();
    if (!proc) return -EPERM;
    if (flags != 0) return -EINVAL;
    if (!nx_ipc_state(proc, 1)) return -ENOMEM;
    if (nx_handle_free_slots(proc) < 1) return -EMFILE;

    nx_mempressure_t *mp = (nx_mempressure_t *)kzalloc(sizeof(nx_mempressure_t));
    if (!mp) {
        serial_puts("[MEM] mem.pressure_open refused: out of kernel heap\n");
        return -ENOMEM;
    }
    mp->hdr.type = NX_OBJ_MEMPRESSURE;
    mp->hdr.destroy = pressure_destroy;
    mp->seen_seq = pmm_pressure_seq();      /* READABLE only after a change */

    int32_t h = nx_handle_alloc(proc, &mp->hdr, PRESSURE_RIGHTS);
    if (h < 0) {
        mp->hdr.refs = 1;
        nx_object_unref(&mp->hdr);
        return h;
    }
    return h;
}

/* int mem_pressure_read(nx_handle_t h) */
int64_t sys_mem_pressure_read_impl(uint64_t h, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5) {
    (void)a2; (void)a3; (void)a4; (void)a5;
    process_t *proc = process_current();
    if (!proc) return -EPERM;
    if (h > 0x7FFFFFFFULL) return -EBADF;

    nx_object_t *obj = NULL;
    int rc = nx_handle_get(proc, (int32_t)h, NX_OBJ_MEMPRESSURE, NX_RIGHT_READ, &obj, NULL);
    if (rc != 0) return rc;

    nx_mempressure_t *mp = (nx_mempressure_t *)obj;
    mp->seen_seq = pmm_pressure_seq();
    return (int64_t)pmm_pressure_level();
}

/* int mem_stats(nx_mem_stats_t *buf, uint32_t size) */
int64_t sys_mem_stats_impl(uint64_t ubuf, uint64_t size, uint64_t a3, uint64_t a4, uint64_t a5) {
    (void)a3; (void)a4; (void)a5;
    process_t *proc = process_current();
    if (!proc) return -EPERM;
    if (size < NX_MEM_STATS_V1_SIZE) return -EINVAL;
    uint64_t n = (size < sizeof(nx_mem_stats_t)) ? size : sizeof(nx_mem_stats_t);
    if (!ubuf || syscall_validate_ptr((void *)ubuf, n, 1) != 0) return -EFAULT;

    nx_mem_stats_t st;
    for (uint64_t i = 0; i < sizeof(st); i++) ((uint8_t *)&st)[i] = 0;

    pmm_stats_t ps;
    pmm_get_stats(&ps);
    size_t heap_total = 0, heap_used = 0, heap_free = 0;
    heap_stats(&heap_total, &heap_used, &heap_free);

    uint64_t user_pages = 0;
    for (uint32_t pid = 1; pid < MAX_PROCESSES; pid++) {
        process_t *p = process_get(pid);
        if (p && p->as) user_pages += p->as->pages_mapped;
    }

    st.size = (uint32_t)n;
    st.version = 1;
    st.level = pmm_pressure_level();
    st.total_bytes = ps.total_memory;
    st.free_bytes = ps.free_pages * PAGE_SIZE;
    st.reserve_bytes = pmm_reserve_pages() * PAGE_SIZE;
    st.user_bytes = user_pages * PAGE_SIZE;
    st.shm_bytes = nx_shm_total_bytes();
    st.kheap_bytes = heap_used;
    st.refusals = addrspace_reserve_refusals();
    st.self_bytes = proc->as ? proc->as->pages_mapped * PAGE_SIZE : 0;

    if (syscall_copy_to_user((void *)ubuf, &st, n) != 0) return -EFAULT;
    return 0;
}
