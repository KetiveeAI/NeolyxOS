/*
 * NeolyxOS Per-Process User Address Spaces
 *
 * Every user process owns a private set of page tables. The kernel's
 * identity map (0-4GB, supervisor-only) is shared by all of them, and the
 * user window [USER_WINDOW_BASE, USER_WINDOW_END) is private: the same
 * virtual address in two processes maps to different physical frames.
 *
 * Physical memory inside the user window range is reserved at PMM init and
 * never handed out, so the kernel never reaches a physical frame through an
 * identity address that a process has remapped. Frames that back user pages
 * come from the PMM (outside the window) and the kernel writes them through
 * their identity-mapped physical address.
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#ifndef NEOLYX_ADDRSPACE_H
#define NEOLYX_ADDRSPACE_H

#include <stdint.h>
#include <stddef.h>

/* User window: 16MB - 256MB. Matches the user link address (0x1000000). */
#define USER_WINDOW_BASE    0x0000000001000000ULL
#define USER_WINDOW_END     0x0000000010000000ULL

/* User stack sits at the top of the window, below one unmapped guard page */
#define USER_STACK_TOP      (USER_WINDOW_END - 0x1000ULL)
#define USER_STACK_SIZE_MAX (512ULL * 1024)

/* Heap reserve per process (grown on demand by brk) */
#define USER_HEAP_RESERVE   (64ULL * 1024 * 1024)

/* Mapping flags for addrspace_map_alloc() / addrspace_map_shared() */
#define AS_MAP_WRITE        0x1

/*
 * Shared region: 512 GiB per process at PML4 slot 128, where shm objects
 * are mapped (docs/desktop/proposals/kernel-shm-ipc.md). It lies outside
 * the kernel identity map, so it needs no PMM reservation. Its PDPT, PDs
 * and PTs are private to the process and allocated on first use. Leaf
 * entries carry AS_PTE_SHARED: the frames belong to an shm object, never
 * to the address space, and teardown never frees them.
 */
#define AS_SHARED_BASE      0x0000400000000000ULL
#define AS_SHARED_END       0x0000408000000000ULL

typedef struct addrspace {
    uint64_t pml4_phys;         /* Private PML4 (loaded into CR3) */
    uint64_t pdpt_phys;         /* Private PDPT for PML4[0] */
    uint64_t pd_phys;           /* Private PD for 0-1GB */
    uint64_t pages_mapped;      /* User frames owned by this space */
    uint64_t shared_pdpt_phys;  /* PDPT for the shared region (0 = none yet) */
    uint64_t shared_pages;      /* Pages currently mapped in the shared region */
    uint32_t reserve_class;     /* AS_CLASS_*: how deep user mappings may dig into the PMM reserve */
} addrspace_t;

/* addrspace_t.reserve_class (K0b memory reserve, see pmm_user_alloc_allowed) */
#define AS_CLASS_NORMAL     0   /* apps: stop at the full reserve */
#define AS_CLASS_SYSTEM     1   /* PROC_FLAG_SYSTEM: may use the upper half */

/**
 * Reserve the physical range that aliases the user window.
 * Must be called once, right after the PMM is initialised and before any
 * other allocation, so no kernel data ever lives there.
 */
void addrspace_reserve_window(void);

/**
 * Capture the active kernel page tables as the template for new spaces and
 * unmap the user window from them (kernel-only CR3 traps stray accesses).
 * Call after paging_activate().
 *
 * @return 0 on success, -1 if paging is not active
 */
int addrspace_init(void);

/** Physical address of the kernel-only PML4 (used by kernel threads). */
uint64_t addrspace_kernel_pml4(void);

/**
 * Create an empty user address space.
 * @return New address space, or NULL when out of memory.
 */
addrspace_t *addrspace_create(void);

/**
 * Free every user frame and page table of a space.
 * The space must not be the one loaded in CR3.
 */
void addrspace_destroy(addrspace_t *as);

/**
 * Map zero-filled pages over [va, va+size). Pages that are already mapped
 * are kept (segments may share a page). Both ends are page-rounded.
 *
 * @return 0 on success, -1 on bad range or out of memory
 */
int addrspace_map_alloc(addrspace_t *as, uint64_t va, uint64_t size, uint32_t flags);

/**
 * Copy kernel memory into a space (target pages must be mapped).
 * @return 0 on success, -1 if any target page is unmapped
 */
int addrspace_copy_in(addrspace_t *as, uint64_t va, const void *src, uint64_t len);

/**
 * Check that [va, va+len) lies in the user window or in the shared region
 * and every page is mapped (and writable when write != 0).
 * @return 0 when valid, -1 otherwise
 */
int addrspace_check_range(const addrspace_t *as, uint64_t va, uint64_t len, int write);

/**
 * Whether the shared region could be set up at boot (its PML4 slot was
 * empty in the kernel tables). When 0, shared mappings are refused.
 */
int addrspace_shared_ok(void);

/**
 * Map existing frames at va in the shared region. va must be page-aligned
 * and [va, va + count pages) must lie in the region and be unmapped. On
 * failure nothing stays mapped (page tables allocated on the way may stay;
 * they are freed with the space).
 *
 * @return 0 on success, -1 on bad range, overlap or out of memory
 */
int addrspace_map_shared(addrspace_t *as, uint64_t va, const uint64_t *frames,
                         uint64_t count, uint32_t flags);

/**
 * Remove count pages at va from the shared region. The frames are not
 * freed (they belong to their shm object). Page tables left empty are
 * freed. TLB entries are flushed when the space is the active one.
 */
void addrspace_unmap_shared(addrspace_t *as, uint64_t va, uint64_t count);

/** Load a space into CR3 (NULL loads the kernel-only tables). */
void addrspace_activate(const addrspace_t *as);

/** User mappings refused at the memory reserve since boot (mem.stats). */
uint64_t addrspace_reserve_refusals(void);

#endif /* NEOLYX_ADDRSPACE_H */
