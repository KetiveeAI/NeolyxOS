/*
 * NeolyxOS Kernel Objects and Handles
 *
 * See include/core/nx_handle.h for the model and
 * docs/desktop/proposals/kernel-shm-ipc.md for the ABI.
 *
 * Syscalls: handle.close (140), handle.dup (149).
 * Channels, obj.wait and ports live in nx_chan.c, shared memory in nx_shm.c.
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include "core/nx_handle.h"
#include "core/process.h"
#include "core/syscall.h"
#include "mm/kheap.h"

extern void serial_puts(const char *s);
extern void serial_putc(char c);

#define HANDLE_SLOT_BITS    8
#define HANDLE_SLOT_MASK    0xFFu
#define HANDLE_GEN_MAX      0x7FFFu   /* keeps every handle value positive */

_Static_assert(NX_HANDLE_MAX == (1 << HANDLE_SLOT_BITS), "handle encoding assumes 256 slots");

static void h_dec(uint64_t v) {
    char buf[21];
    int i = 20;
    buf[i] = '\0';
    if (v == 0) { serial_putc('0'); return; }
    while (v > 0 && i > 0) { buf[--i] = (char)('0' + (v % 10)); v /= 10; }
    serial_puts(&buf[i]);
}

static void h_log_pid(const char *prefix, struct process *proc, const char *msg) {
    serial_puts(prefix);
    serial_puts(" PID ");
    h_dec(proc ? proc->pid : 0);
    serial_puts(": ");
    serial_puts(msg);
    serial_puts("\n");
}

/* ============ Objects ============ */

void nx_object_ref(nx_object_t *obj) {
    if (!obj) return;
    if (obj->refs == 0xFFFFFFFFu) {
        /* Cannot happen within the limits (256 handles x 256 processes plus
         * 128 mappings each); refuse to wrap rather than free early. */
        serial_puts("[HANDLE] ERROR: object reference count saturated\n");
        return;
    }
    obj->refs++;
}

/* Objects whose last reference is gone, waiting for destroy() */
static nx_object_t *g_destroy_head = NULL;
static int g_destroying = 0;

void nx_object_unref(nx_object_t *obj) {
    if (!obj) return;
    if (obj->refs == 0) {
        serial_puts("[HANDLE] ERROR: unref of an object with no references\n");
        return;
    }
    obj->refs--;
    if (obj->refs != 0 || !obj->destroy) return;

    /* Queue it; the outermost call drains the queue. A destroy() that drops
     * more last references only appends to the queue. */
    obj->dnext = g_destroy_head;
    g_destroy_head = obj;
    if (g_destroying) return;

    g_destroying = 1;
    while (g_destroy_head) {
        nx_object_t *o = g_destroy_head;
        g_destroy_head = o->dnext;
        o->dnext = NULL;
        o->destroy(o);
    }
    g_destroying = 0;
}

/* ============ Handle table ============ */

nx_proc_ipc_t *nx_ipc_state(struct process *proc, int create) {
    if (!proc) return NULL;
    if (!proc->ipc && create) {
        proc->ipc = (nx_proc_ipc_t *)kzalloc(sizeof(nx_proc_ipc_t));
        if (!proc->ipc) {
            h_log_pid("[HANDLE]", proc, "cannot allocate IPC state");
        }
    }
    return (nx_proc_ipc_t *)proc->ipc;
}

int32_t nx_handle_alloc(struct process *proc, nx_object_t *obj, uint32_t rights) {
    if (!proc || !obj) return -EINVAL;
    nx_proc_ipc_t *ipc = nx_ipc_state(proc, 1);
    if (!ipc) return -ENOMEM;

    for (uint32_t i = 0; i < NX_HANDLE_MAX; i++) {
        nx_handle_slot_t *slot = &ipc->slots[i];
        if (slot->obj) continue;

        uint16_t gen = (uint16_t)(slot->gen + 1);
        if (gen == 0 || gen > HANDLE_GEN_MAX) gen = 1;
        slot->gen = gen;
        slot->obj = obj;
        slot->rights = rights & NX_RIGHTS_ALL;
        nx_object_ref(obj);
        ipc->handle_count++;
        return (int32_t)(((uint32_t)gen << HANDLE_SLOT_BITS) | i);
    }

    h_log_pid("[HANDLE]", proc, "handle table full (256)");
    return -EMFILE;
}

/* Slot for a live handle value, or NULL */
static nx_handle_slot_t *handle_slot(struct process *proc, int32_t handle) {
    if (!proc || handle <= 0) return NULL;
    nx_proc_ipc_t *ipc = nx_ipc_state(proc, 0);
    if (!ipc) return NULL;

    uint32_t idx = (uint32_t)handle & HANDLE_SLOT_MASK;
    uint32_t gen = (uint32_t)handle >> HANDLE_SLOT_BITS;
    nx_handle_slot_t *slot = &ipc->slots[idx];
    if (!slot->obj || slot->gen != gen) return NULL;
    return slot;
}

int nx_handle_get(struct process *proc, int32_t handle, uint32_t type,
                  uint32_t required, nx_object_t **obj, uint32_t *rights) {
    nx_handle_slot_t *slot = handle_slot(proc, handle);
    if (!slot) return -EBADF;
    if (type && slot->obj->type != type) return -EINVAL;
    if ((slot->rights & required) != required) {
        h_log_pid("[HANDLE] DENIED", proc, "handle lacks a required right (-EACCES)");
        return -EACCES;
    }
    if (obj) *obj = slot->obj;
    if (rights) *rights = slot->rights;
    return 0;
}

int nx_handle_close(struct process *proc, int32_t handle) {
    nx_handle_slot_t *slot = handle_slot(proc, handle);
    if (!slot) return -EBADF;

    nx_object_t *obj = slot->obj;
    slot->obj = NULL;
    slot->rights = 0;
    nx_proc_ipc_t *ipc = (nx_proc_ipc_t *)proc->ipc;
    if (ipc->handle_count) ipc->handle_count--;
    nx_object_unref(obj);
    return 0;
}

int nx_handle_take(struct process *proc, int32_t handle, nx_object_t **obj, uint32_t *rights) {
    nx_handle_slot_t *slot = handle_slot(proc, handle);
    if (!slot) return -EBADF;
    if (obj) *obj = slot->obj;
    if (rights) *rights = slot->rights;
    slot->obj = NULL;
    slot->rights = 0;
    nx_proc_ipc_t *ipc = (nx_proc_ipc_t *)proc->ipc;
    if (ipc->handle_count) ipc->handle_count--;
    return 0;
}

uint32_t nx_handle_free_slots(struct process *proc) {
    nx_proc_ipc_t *ipc = nx_ipc_state(proc, 0);
    if (!ipc) return NX_HANDLE_MAX;
    return NX_HANDLE_MAX - ipc->handle_count;
}

void nx_ipc_wake_waiters(void) {
    for (uint32_t pid = 1; pid < MAX_PROCESSES; pid++) {
        process_t *p = process_get(pid);
        if (!p || !p->ipc) continue;
        if (((nx_proc_ipc_t *)p->ipc)->waiting) process_wake_sleeper(p);
    }
}

void nx_ipc_release(struct process *proc) {
    if (!proc) return;
    nx_proc_ipc_t *ipc = (nx_proc_ipc_t *)proc->ipc;

    if (ipc) {
        uint32_t maps = ipc->map_count;
        uint32_t handles = 0;

        /* Mappings first: they reference the address space, which the
         * caller destroys right after this */
        nx_shm_unmap_all(proc, ipc);

        for (uint32_t i = 0; i < NX_HANDLE_MAX; i++) {
            nx_handle_slot_t *slot = &ipc->slots[i];
            if (!slot->obj) continue;
            nx_object_t *obj = slot->obj;
            slot->obj = NULL;
            slot->rights = 0;
            nx_object_unref(obj);
            handles++;
        }

        if (maps || handles) {
            serial_puts("[HANDLE] PID ");
            h_dec(proc->pid);
            serial_puts(": released ");
            h_dec(handles);
            serial_puts(" handle(s) and ");
            h_dec(maps);
            serial_puts(" mapping(s)\n");
        }
        proc->ipc = NULL;
        kfree(ipc);
    }

    /* Objects it created may outlive it (held by others): stop charging it */
    nx_shm_forget_creator(proc);
    nx_chan_forget_sender(proc);
}

/* ============ Syscalls ============ */

/* int sys_handle_close(nx_handle_t h) */
int64_t sys_handle_close_impl(uint64_t h, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5) {
    (void)a2; (void)a3; (void)a4; (void)a5;
    if (h > (uint64_t)0x7FFFFFFF) return -EBADF;
    return nx_handle_close(process_current(), (int32_t)h);
}

/* nx_handle_t sys_handle_dup(nx_handle_t h, uint32_t rights) */
int64_t sys_handle_dup_impl(uint64_t h, uint64_t rights, uint64_t a3, uint64_t a4, uint64_t a5) {
    (void)a3; (void)a4; (void)a5;
    struct process *proc = process_current();
    if (h > (uint64_t)0x7FFFFFFF) return -EBADF;

    nx_object_t *obj = NULL;
    uint32_t have = 0;
    int rc = nx_handle_get(proc, (int32_t)h, 0, NX_RIGHT_DUPLICATE, &obj, &have);
    if (rc != 0) return rc;

    uint32_t want;
    if (rights == NX_RIGHTS_SAME) {
        want = have;
    } else {
        if (rights & ~(uint64_t)NX_RIGHTS_ALL) return -EINVAL;
        want = (uint32_t)rights;
        if ((want & have) != want) {
            h_log_pid("[HANDLE] DENIED", proc, "handle.dup asked for rights it does not hold (-EACCES)");
            return -EACCES;
        }
    }
    return nx_handle_alloc(proc, obj, want);
}
