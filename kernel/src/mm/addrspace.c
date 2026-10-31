/*
 * NeolyxOS Per-Process User Address Spaces
 *
 * See include/mm/addrspace.h for the model. Layout of one space:
 *
 *   PML4 (private) --[0]--> PDPT (private) --[0]--> PD (private, 0-1GB)
 *     other PML4 entries     other PDPT entries      PD[8..127]  -> private PTs
 *     copied from kernel     copied from kernel      (user window, 16-256MB)
 *                                                    other PD entries copied
 *                                                    from the kernel PD
 *
 * Kernel identity mappings are supervisor-only, so user code can only reach
 * its own window pages (plus the framebuffer, which the kernel maps with
 * PAGE_USER in the shared tables for the desktop compositor).
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include "mm/addrspace.h"
#include "mm/pmm.h"
#include "mm/kheap.h"

extern void serial_puts(const char *s);
extern void serial_putc(char c);
extern uint64_t paging_get_pml4(void);

/* ============ Constants ============ */

#define PTE_PRESENT     (1ULL << 0)
#define PTE_WRITE       (1ULL << 1)
#define PTE_USER        (1ULL << 2)
#define PTE_LARGE       (1ULL << 7)
#define PTE_ADDR_MASK   0x000FFFFFFFFFF000ULL

#define AS_PAGE         0x1000ULL
#define IDENTITY_LIMIT  0x100000000ULL   /* Kernel identity map covers 0-4GB */

#define WINDOW_PD_FIRST ((USER_WINDOW_BASE >> 21) & 0x1FF)   /* 8   */
#define WINDOW_PD_END   ((USER_WINDOW_END >> 21) & 0x1FF)    /* 128 */

/* ============ State ============ */

static uint64_t g_kernel_pml4 = 0;
static int g_window_reserved = 0;

/* ============ Helpers ============ */

static void as_hex64(uint64_t v) {
    const char *hex = "0123456789ABCDEF";
    serial_puts("0x");
    for (int i = 60; i >= 0; i -= 4) serial_putc(hex[(v >> i) & 0xF]);
}

static void as_dec(uint64_t v) {
    char buf[21];
    int i = 20;
    buf[i] = '\0';
    if (v == 0) { serial_putc('0'); return; }
    while (v > 0 && i > 0) { buf[--i] = (char)('0' + (v % 10)); v /= 10; }
    serial_puts(&buf[i]);
}

static void as_zero_page(uint64_t phys) {
    uint64_t *p = (uint64_t *)phys;
    for (int i = 0; i < 512; i++) p[i] = 0;
}

static void as_copy_bytes(uint8_t *dst, const uint8_t *src, uint64_t n) {
    while (n--) *dst++ = *src++;
}

/*
 * Allocate one zeroed frame the kernel can reach through its identity map.
 * Returns 0 on failure.
 */
static uint64_t as_alloc_frame(void) {
    uint64_t phys = pmm_alloc_page();
    if (phys == 0) {
        serial_puts("[AS] ERROR: out of physical memory\n");
        return 0;
    }
    if (phys + AS_PAGE > IDENTITY_LIMIT) {
        /* Not reachable by the kernel: refuse rather than corrupt */
        serial_puts("[AS] ERROR: frame above identity map: ");
        as_hex64(phys);
        serial_puts("\n");
        pmm_free_page(phys);
        return 0;
    }
    if (phys < USER_WINDOW_END && phys + AS_PAGE > USER_WINDOW_BASE) {
        serial_puts("[AS] ERROR: PMM returned a frame inside the user window\n");
        return 0;
    }
    as_zero_page(phys);
    return phys;
}

static inline int as_va_in_window(uint64_t va) {
    return va >= USER_WINDOW_BASE && va < USER_WINDOW_END;
}

/* Return the PTE for va, or NULL if its page table does not exist */
static uint64_t *as_find_pte(const addrspace_t *as, uint64_t va) {
    uint64_t *pd = (uint64_t *)as->pd_phys;
    uint64_t pde = pd[(va >> 21) & 0x1FF];
    if (!(pde & PTE_PRESENT) || (pde & PTE_LARGE)) return NULL;
    uint64_t *pt = (uint64_t *)(pde & PTE_ADDR_MASK);
    return &pt[(va >> 12) & 0x1FF];
}

/* ============ Public API ============ */

void addrspace_reserve_window(void) {
    if (g_window_reserved) return;
    pmm_reserve_region(USER_WINDOW_BASE, USER_WINDOW_END - USER_WINDOW_BASE);
    g_window_reserved = 1;
    serial_puts("[AS] Reserved physical alias of user window ");
    as_hex64(USER_WINDOW_BASE);
    serial_puts(" - ");
    as_hex64(USER_WINDOW_END);
    serial_puts("\n");
}

int addrspace_init(void) {
    uint64_t pml4_phys = paging_get_pml4();
    if (pml4_phys == 0) {
        serial_puts("[AS] ERROR: kernel page tables not active\n");
        return -1;
    }
    if (!g_window_reserved) {
        serial_puts("[AS] ERROR: user window was never reserved in the PMM\n");
        return -1;
    }

    uint64_t *pml4 = (uint64_t *)pml4_phys;
    if (!(pml4[0] & PTE_PRESENT)) {
        serial_puts("[AS] ERROR: kernel PML4[0] not present\n");
        return -1;
    }
    uint64_t *pdpt = (uint64_t *)(pml4[0] & PTE_ADDR_MASK);
    if (!(pdpt[0] & PTE_PRESENT) || (pdpt[0] & PTE_LARGE)) {
        serial_puts("[AS] ERROR: kernel 0-1GB is not mapped with a PD\n");
        return -1;
    }
    uint64_t *pd = (uint64_t *)(pdpt[0] & PTE_ADDR_MASK);

    /* Remove the window from the kernel-only tables */
    for (uint64_t i = WINDOW_PD_FIRST; i < WINDOW_PD_END; i++) {
        if ((pd[i] & PTE_PRESENT) && !(pd[i] & PTE_LARGE)) {
            /* A page table split earlier: it only described the alias */
            pmm_free_page(pd[i] & PTE_ADDR_MASK);
        }
        pd[i] = 0;
    }
    __asm__ volatile("mov %0, %%cr3" : : "r"(pml4_phys) : "memory");

    g_kernel_pml4 = pml4_phys;
    serial_puts("[AS] Kernel template PML4 at ");
    as_hex64(pml4_phys);
    serial_puts(", user window unmapped from kernel tables\n");
    return 0;
}

uint64_t addrspace_kernel_pml4(void) {
    return g_kernel_pml4;
}

addrspace_t *addrspace_create(void) {
    if (g_kernel_pml4 == 0) {
        serial_puts("[AS] ERROR: addrspace_create before addrspace_init\n");
        return NULL;
    }

    addrspace_t *as = (addrspace_t *)kzalloc(sizeof(addrspace_t));
    if (!as) {
        serial_puts("[AS] ERROR: cannot allocate address space descriptor\n");
        return NULL;
    }

    as->pml4_phys = as_alloc_frame();
    as->pdpt_phys = as_alloc_frame();
    as->pd_phys = as_alloc_frame();
    if (!as->pml4_phys || !as->pdpt_phys || !as->pd_phys) {
        if (as->pml4_phys) pmm_free_page(as->pml4_phys);
        if (as->pdpt_phys) pmm_free_page(as->pdpt_phys);
        if (as->pd_phys) pmm_free_page(as->pd_phys);
        kfree(as);
        return NULL;
    }

    const uint64_t *k_pml4 = (const uint64_t *)g_kernel_pml4;
    const uint64_t *k_pdpt = (const uint64_t *)(k_pml4[0] & PTE_ADDR_MASK);
    const uint64_t *k_pd = (const uint64_t *)(k_pdpt[0] & PTE_ADDR_MASK);

    uint64_t *pml4 = (uint64_t *)as->pml4_phys;
    uint64_t *pdpt = (uint64_t *)as->pdpt_phys;
    uint64_t *pd = (uint64_t *)as->pd_phys;

    for (int i = 0; i < 512; i++) {
        pml4[i] = k_pml4[i];
        pdpt[i] = k_pdpt[i];
        pd[i] = k_pd[i];
    }
    for (uint64_t i = WINDOW_PD_FIRST; i < WINDOW_PD_END; i++) {
        pd[i] = 0;
    }
    pdpt[0] = as->pd_phys | PTE_PRESENT | PTE_WRITE | PTE_USER;
    pml4[0] = as->pdpt_phys | PTE_PRESENT | PTE_WRITE | PTE_USER;

    return as;
}

void addrspace_destroy(addrspace_t *as) {
    if (!as) return;

    uint64_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    if ((cr3 & PTE_ADDR_MASK) == as->pml4_phys) {
        /* Freeing live tables would fault on the next instruction fetch */
        serial_puts("[AS] ERROR: refusing to destroy the active address space\n");
        return;
    }

    uint64_t *pd = (uint64_t *)as->pd_phys;
    uint64_t frames = 0;
    for (uint64_t i = WINDOW_PD_FIRST; i < WINDOW_PD_END; i++) {
        if (!(pd[i] & PTE_PRESENT)) continue;
        uint64_t *pt = (uint64_t *)(pd[i] & PTE_ADDR_MASK);
        for (int j = 0; j < 512; j++) {
            if (pt[j] & PTE_PRESENT) {
                pmm_free_page(pt[j] & PTE_ADDR_MASK);
                frames++;
            }
        }
        pmm_free_page((uint64_t)pt);
        pd[i] = 0;
    }

    pmm_free_page(as->pd_phys);
    pmm_free_page(as->pdpt_phys);
    pmm_free_page(as->pml4_phys);

    serial_puts("[AS] Destroyed address space, freed ");
    as_dec(frames);
    serial_puts(" user frames\n");

    kfree(as);
}

int addrspace_map_alloc(addrspace_t *as, uint64_t va, uint64_t size, uint32_t flags) {
    if (!as || size == 0) return -1;

    uint64_t start = va & ~(AS_PAGE - 1);
    uint64_t end = (va + size + AS_PAGE - 1) & ~(AS_PAGE - 1);
    if (end < start || start < USER_WINDOW_BASE || end > USER_WINDOW_END) {
        serial_puts("[AS] ERROR: map outside user window: ");
        as_hex64(va);
        serial_puts(" +");
        as_hex64(size);
        serial_puts("\n");
        return -1;
    }

    uint64_t leaf = PTE_PRESENT | PTE_USER | ((flags & AS_MAP_WRITE) ? PTE_WRITE : 0);
    uint64_t *pd = (uint64_t *)as->pd_phys;

    for (uint64_t page = start; page < end; page += AS_PAGE) {
        uint64_t pdi = (page >> 21) & 0x1FF;
        if (!(pd[pdi] & PTE_PRESENT)) {
            uint64_t pt_phys = as_alloc_frame();
            if (!pt_phys) return -1;
            pd[pdi] = pt_phys | PTE_PRESENT | PTE_WRITE | PTE_USER;
        }
        uint64_t *pt = (uint64_t *)(pd[pdi] & PTE_ADDR_MASK);
        uint64_t *pte = &pt[(page >> 12) & 0x1FF];

        if (*pte & PTE_PRESENT) {
            /* Shared page between segments: widen permissions if needed */
            *pte |= (leaf & PTE_WRITE);
            continue;
        }

        uint64_t frame = as_alloc_frame();
        if (!frame) return -1;
        *pte = frame | leaf;
        as->pages_mapped++;
    }
    return 0;
}

int addrspace_copy_in(addrspace_t *as, uint64_t va, const void *src, uint64_t len) {
    if (!as || (!src && len)) return -1;
    if (va + len < va) return -1;

    const uint8_t *s = (const uint8_t *)src;
    while (len > 0) {
        if (!as_va_in_window(va)) return -1;
        uint64_t *pte = as_find_pte(as, va);
        if (!pte || !(*pte & PTE_PRESENT)) return -1;

        uint64_t off = va & (AS_PAGE - 1);
        uint64_t chunk = AS_PAGE - off;
        if (chunk > len) chunk = len;

        as_copy_bytes((uint8_t *)((*pte & PTE_ADDR_MASK) + off), s, chunk);
        va += chunk;
        s += chunk;
        len -= chunk;
    }
    return 0;
}

int addrspace_check_range(const addrspace_t *as, uint64_t va, uint64_t len, int write) {
    if (!as) return -1;
    if (len == 0) len = 1;
    uint64_t end = va + len;
    if (end < va) return -1;
    if (va < USER_WINDOW_BASE || end > USER_WINDOW_END) return -1;

    for (uint64_t page = va & ~(AS_PAGE - 1); page < end; page += AS_PAGE) {
        uint64_t *pte = as_find_pte(as, page);
        if (!pte) return -1;
        if ((*pte & (PTE_PRESENT | PTE_USER)) != (PTE_PRESENT | PTE_USER)) return -1;
        if (write && !(*pte & PTE_WRITE)) return -1;
    }
    return 0;
}

void addrspace_activate(const addrspace_t *as) {
    uint64_t target = as ? as->pml4_phys : g_kernel_pml4;
    if (target == 0) return;
    uint64_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    if ((cr3 & PTE_ADDR_MASK) != target) {
        __asm__ volatile("mov %0, %%cr3" : : "r"(target) : "memory");
    }
}
