/*
 * NeolyxOS App Classes and Memory Pressure ABI (public)
 *
 * ABI 1.3 (K1):
 *   153 proc.set_class     int proc_set_class(uint32_t pid, uint32_t cls)
 *   154 mem.pressure_open  nx_handle_t mem_pressure_open(uint32_t flags)
 *   155 mem.pressure_read  int mem_pressure_read(nx_handle_t h)
 *   156 mem.stats          int mem_stats(nx_mem_stats_t *buf, uint32_t size)
 *
 * Spec: docs/kernel/app-classes-and-memory-pressure.md (K1, approved by the
 * owner 2026-11-01). Layouts here are frozen once 1.0 ships.
 *
 * K2 part 3 added NX_EXIT_RECLAIMED (docs/kernel/memory-policy.md). It is
 * an exit status value only, with no new call, so NX_ABI_MINOR stays 3.
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#ifndef NEOLYX_NX_MEM_H
#define NEOLYX_NX_MEM_H

#include <stdint.h>

/* proc.set_class classes. System processes (the desktop) and kernel
 * threads have their own class, which this call cannot set or change. */
#define NX_CLASS_FOREGROUND     1u      /* default for new apps */
#define NX_CLASS_BACKGROUND     2u

/* Memory pressure levels (mem.pressure_read, nx_mem_stats_t.level).
 * R = the kernel's memory reserve (nx_mem_stats_t.reserve_bytes). */
#define NX_MEM_NORMAL           0u
#define NX_MEM_WARN             1u      /* free <= 3R; back to NORMAL above 4R */
#define NX_MEM_CRITICAL         2u      /* free <= 1.5R; back to WARN above 2R */

/* mem.stats output. The kernel fills min(size, sizeof) bytes; sizes below
 * NX_MEM_STATS_V1_SIZE are refused, so fields can be appended later. */
typedef struct {
    uint32_t size;            /* bytes the kernel filled */
    uint32_t version;         /* 1 */
    uint32_t level;           /* NX_MEM_* */
    uint32_t reserved;        /* 0 */
    uint64_t total_bytes;     /* physical memory managed by the kernel */
    uint64_t free_bytes;      /* free physical memory */
    uint64_t reserve_bytes;   /* memory ordinary apps cannot take */
    uint64_t user_bytes;      /* private pages of all user processes */
    uint64_t shm_bytes;       /* live shared memory objects */
    uint64_t kheap_bytes;     /* kernel heap in use */
    uint64_t refusals;        /* user mappings refused at the reserve since boot */
    uint64_t self_bytes;      /* the caller's own private pages */
} nx_mem_stats_t;

#define NX_MEM_STATS_V1_SIZE    80u

/* Exit status of an app the kernel ended to free memory (K2 part 3):
 * a BACKGROUND app, chosen when the desktop or the kernel would otherwise
 * run out. Reserved: apps should not exit with this value themselves. */
#define NX_EXIT_RECLAIMED       251

/* nx_abi_info_t.flags: proc.set_class and mem.* are available */
#define NX_ABI_FLAG_MEMCLASS    0x0008u

_Static_assert(sizeof(nx_mem_stats_t) == 80, "nx_mem_stats_t v1 layout is frozen at 80 bytes");

#endif /* NEOLYX_NX_MEM_H */
