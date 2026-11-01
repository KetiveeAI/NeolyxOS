/*
 * NeolyxOS Kernel Objects and Handles (kernel-internal)
 *
 * Kernel objects (shared memory now; channels and ports next) are reached
 * from userspace only through handles: per-process integers that carry
 * rights. A handle value is (generation << 8) | slot, so a closed and reused
 * slot never matches an old value. Objects are reference-counted and are
 * destroyed when their last handle, mapping or queued reference is gone.
 *
 * Concurrency: one CPU, syscalls run with interrupts disabled and the
 * kernel is not preemptible, so no locks are needed today. Anything that
 * adds SMP or kernel preemption must add locking here first.
 *
 * Public ABI types: core/nx_ipc.h. Spec: docs/desktop/proposals/kernel-shm-ipc.md
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#ifndef NEOLYX_NX_HANDLE_H
#define NEOLYX_NX_HANDLE_H

#include <stdint.h>
#include "core/nx_ipc.h"

struct process;
struct nx_shm;

/* Object types */
#define NX_OBJ_SHM          1
#define NX_OBJ_CHAN         2   /* reserved for step 2 */
#define NX_OBJ_PORT         3   /* reserved for step 2 */

#define NX_RIGHTS_ALL       (NX_RIGHT_READ | NX_RIGHT_WRITE | NX_RIGHT_MAP | \
                             NX_RIGHT_TRANSFER | NX_RIGHT_DUPLICATE)

typedef struct nx_object nx_object_t;

/* Called once, when the last reference is dropped */
typedef void (*nx_object_destroy_fn)(nx_object_t *obj);

struct nx_object {
    uint32_t type;                  /* NX_OBJ_* */
    uint32_t refs;                  /* handles + mappings + queued references */
    nx_object_destroy_fn destroy;
};

typedef struct {
    nx_object_t *obj;               /* NULL = free slot */
    uint32_t rights;
    uint16_t gen;                   /* bumped on every allocation, never 0 in use */
    uint16_t reserved;
} nx_handle_slot_t;

typedef struct {
    uint64_t va;                    /* base returned by shm.map */
    uint64_t span;                  /* reserved VA, 2 MiB multiple (includes the gap) */
    uint64_t pages;                 /* pages actually mapped */
    struct nx_shm *shm;             /* holds one reference */
} nx_shm_mapping_t;

/* Per-process IPC state, allocated on first use (process_t.ipc) */
typedef struct nx_proc_ipc {
    nx_handle_slot_t slots[NX_HANDLE_MAX];
    uint32_t handle_count;
    uint32_t map_count;
    nx_shm_mapping_t maps[NX_SHM_MAP_MAX];
    uint64_t shm_bytes;             /* bytes of live shm objects this process created */
    uint64_t queued_bytes;          /* reserved for step 2 (channel queues) */
} nx_proc_ipc_t;

/* ============ Objects ============ */

void nx_object_ref(nx_object_t *obj);

/** Drop one reference; destroys the object when it was the last one. */
void nx_object_unref(nx_object_t *obj);

/* ============ Handles ============ */

/**
 * Per-process IPC state. With create != 0 it is allocated on first use.
 * @return state, or NULL (no process, or out of memory when creating)
 */
nx_proc_ipc_t *nx_ipc_state(struct process *proc, int create);

/**
 * Install obj in proc's handle table with the given rights. Takes a new
 * reference on obj (the caller keeps its own).
 * @return handle (> 0), -EMFILE (table full) or -ENOMEM
 */
int32_t nx_handle_alloc(struct process *proc, nx_object_t *obj, uint32_t rights);

/**
 * Look up a handle of the calling process.
 * @param type      required NX_OBJ_* type, or 0 for any
 * @param required  rights that must all be present
 * @return 0 and *obj / *rights set; -EBADF (not a live handle), -EINVAL
 *         (wrong type), -EACCES (missing rights)
 */
int nx_handle_get(struct process *proc, int32_t handle, uint32_t type,
                  uint32_t required, nx_object_t **obj, uint32_t *rights);

/** Close one handle. @return 0 or -EBADF */
int nx_handle_close(struct process *proc, int32_t handle);

/**
 * Release everything a process holds: unmap every shm mapping, close every
 * handle, stop charging it for shm it created, and free its IPC state.
 * Called on exit, kill, fault and exec. Safe when the process has none.
 */
void nx_ipc_release(struct process *proc);

/* ============ Shared memory (nx_shm.c) ============ */

/** Unmap every mapping of proc (used by nx_ipc_release). */
void nx_shm_unmap_all(struct process *proc, nx_proc_ipc_t *ipc);

/** Stop charging proc for objects it created that outlive it. */
void nx_shm_forget_creator(struct process *proc);

/* ============ Syscall handlers ============ */

int64_t sys_handle_close_impl(uint64_t h, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5);
int64_t sys_handle_dup_impl(uint64_t h, uint64_t rights, uint64_t a3, uint64_t a4, uint64_t a5);
int64_t sys_shm_create_impl(uint64_t size, uint64_t flags, uint64_t a3, uint64_t a4, uint64_t a5);
int64_t sys_shm_map_impl(uint64_t h, uint64_t prot, uint64_t out_size, uint64_t a4, uint64_t a5);
int64_t sys_shm_unmap_impl(uint64_t addr, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5);

#endif /* NEOLYX_NX_HANDLE_H */
