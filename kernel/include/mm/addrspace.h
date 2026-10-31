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

/* Mapping flags for addrspace_map_alloc() */
#define AS_MAP_WRITE        0x1

typedef struct addrspace {
    uint64_t pml4_phys;         /* Private PML4 (loaded into CR3) */
    uint64_t pdpt_phys;         /* Private PDPT for PML4[0] */
    uint64_t pd_phys;           /* Private PD for 0-1GB */
    uint64_t pages_mapped;      /* User frames owned by this space */
} addrspace_t;

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
 * Check that [va, va+len) lies in the user window and every page is
 * mapped (and writable when write != 0).
 * @return 0 when valid, -1 otherwise
 */
int addrspace_check_range(const addrspace_t *as, uint64_t va, uint64_t len, int write);

/** Load a space into CR3 (NULL loads the kernel-only tables). */
void addrspace_activate(const addrspace_t *as);

#endif /* NEOLYX_ADDRSPACE_H */
