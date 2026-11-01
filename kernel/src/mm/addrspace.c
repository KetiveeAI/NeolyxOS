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
 * The shared region (PML4[128], AS_SHARED_BASE..AS_SHARED_END) gets its own
 * private PDPT on the first shm mapping; its leaf entries carry
 * PTE_SHARED and point at frames owned by shm objects.
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
#define PTE_SHARED      (1ULL << 9)      /* Software bit: frame owned by an shm object */
#define PTE_ADDR_MASK   0x000FFFFFFFFFF000ULL

#define AS_PAGE         0x1000ULL
#define IDENTITY_LIMIT  0x100000000ULL   /* Kernel identity map covers 0-4GB */

#define WINDOW_PD_FIRST ((USER_WINDOW_BASE >> 21) & 0x1FF)   /* 8   */
#define WINDOW_PD_END   ((USER_WINDOW_END >> 21) & 0x1FF)    /* 128 */

#define SHARED_PML4_INDEX ((AS_SHARED_BASE >> 39) & 0x1FF)   /* 128 */

/* ============ State ============ */

static uint64_t g_kernel_pml4 = 0;
static int g_window_reserved = 0;
static int g_shared_ok = 0;

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

/* Decimal for log lines (no formatted printing in the kernel) */
static void as_log_dec(uint64_t v) {
    char buf[21];
    int i = 20;
    buf[i] = '\0';
    if (v == 0) buf[--i] = '0';
    while (v > 0 && i > 0) { buf[--i] = (char)('0' + v % 10); v /= 10; }
    serial_puts(&buf[i]);
}

/*
 * K0b memory reserve: may a user mapping take one more frame? Logs the
 * refusal; the caller (brk, spawn, exec) logs which process it was.
 */
static int as_reserve_allows(int system_class) {
    if (pmm_user_alloc_allowed(1, system_class)) return 1;
    pmm_stats_t st;
    pmm_get_stats(&st);
    serial_puts("[MEM] user mapping refused: ");
    as_log_dec(st.free_pages * PAGE_SIZE / 1024);
    serial_puts(" KB free, reserve ");
    as_log_dec(pmm_reserve_pages() * PAGE_SIZE / 1024);
    serial_puts(system_class ? " KB (system class may use half)\n" : " KB\n");
    return 0;
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

static inline int as_va_in_shared(uint64_t va) {
    return va >= AS_SHARED_BASE && va < AS_SHARED_END;
}

/*
 * Return the PTE for va in the shared region. When create is non-zero,
 * missing PDPT/PD/PT levels are allocated (zeroed). Returns NULL when a
 * level is missing (create == 0) or cannot be allocated.
 */
static uint64_t *as_shared_pte(addrspace_t *as, uint64_t va, int create) {
    if (!as || !as_va_in_shared(va)) return NULL;

    uint64_t *pml4 = (uint64_t *)as->pml4_phys;
    if (!as->shared_pdpt_phys) {
        if (!create) return NULL;
        uint64_t f = as_alloc_frame();
        if (!f) return NULL;
        as->shared_pdpt_phys = f;
        pml4[SHARED_PML4_INDEX] = f | PTE_PRESENT | PTE_WRITE | PTE_USER;
    }

    uint64_t *pdpt = (uint64_t *)as->shared_pdpt_phys;
    uint64_t *pdpte = &pdpt[(va >> 30) & 0x1FF];
    if (!(*pdpte & PTE_PRESENT)) {
        if (!create) return NULL;
        uint64_t f = as_alloc_frame();
        if (!f) return NULL;
        *pdpte = f | PTE_PRESENT | PTE_WRITE | PTE_USER;
    }

    uint64_t *pd = (uint64_t *)(*pdpte & PTE_ADDR_MASK);
    uint64_t *pde = &pd[(va >> 21) & 0x1FF];
    if (!(*pde & PTE_PRESENT)) {
        if (!create) return NULL;
        uint64_t f = as_alloc_frame();
        if (!f) return NULL;
        *pde = f | PTE_PRESENT | PTE_WRITE | PTE_USER;
    }

    uint64_t *pt = (uint64_t *)(*pde & PTE_ADDR_MASK);
    return &pt[(va >> 12) & 0x1FF];
}

/* Const lookup for the shared region (never allocates) */
static const uint64_t *as_shared_pte_ro(const addrspace_t *as, uint64_t va) {
    return as_shared_pte((addrspace_t *)as, va, 0);
}

/* Free the shared region's page tables. Shared frames are never freed here:
 * the shm layer unmaps every mapping before a space is destroyed. A leaf
 * still present is logged and its frame leaked rather than double-freed. */
static void as_destroy_shared(addrspace_t *as) {
    if (!as->shared_pdpt_phys) return;

    uint64_t stray = 0;
    uint64_t *pdpt = (uint64_t *)as->shared_pdpt_phys;
    for (int i = 0; i < 512; i++) {
        if (!(pdpt[i] & PTE_PRESENT)) continue;
        uint64_t *pd = (uint64_t *)(pdpt[i] & PTE_ADDR_MASK);
        for (int j = 0; j < 512; j++) {
            if (!(pd[j] & PTE_PRESENT)) continue;
            uint64_t *pt = (uint64_t *)(pd[j] & PTE_ADDR_MASK);
            for (int k = 0; k < 512; k++) {
                if (pt[k] & PTE_PRESENT) stray++;
            }
            pmm_free_page((uint64_t)pt);
        }
        pmm_free_page((uint64_t)pd);
    }
    pmm_free_page(as->shared_pdpt_phys);
    as->shared_pdpt_phys = 0;

    if (stray) {
        serial_puts("[AS] WARNING: ");
        as_dec(stray);
        serial_puts(" shared pages still mapped at teardown (frames left to their shm objects)\n");
    }
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

    /* The shared region needs its PML4 slot free in every space. An
     * identity-mapped MMIO range there (e.g. a GOP framebuffer at 64 TiB+)
     * would collide: refuse shared memory rather than map over it. */
    if (pml4[SHARED_PML4_INDEX] & PTE_PRESENT) {
        g_shared_ok = 0;
        serial_puts("[AS] ERROR: PML4 slot for the shared region is in use; "
                    "shared memory disabled\n");
    } else {
        g_shared_ok = 1;
        serial_puts("[AS] Shared region ");
        as_hex64(AS_SHARED_BASE);
        serial_puts(" - ");
        as_hex64(AS_SHARED_END);
        serial_puts(" available\n");
    }

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

    as_destroy_shared(as);

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
    int system_class = (as->reserve_class == AS_CLASS_SYSTEM);

    for (uint64_t page = start; page < end; page += AS_PAGE) {
        uint64_t pdi = (page >> 21) & 0x1FF;
        if (!(pd[pdi] & PTE_PRESENT)) {
            if (!as_reserve_allows(system_class)) return -1;
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

        if (!as_reserve_allows(system_class)) return -1;
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

    int shared = (va >= AS_SHARED_BASE && end <= AS_SHARED_END);
    if (!shared && (va < USER_WINDOW_BASE || end > USER_WINDOW_END)) return -1;

    for (uint64_t page = va & ~(AS_PAGE - 1); page < end; page += AS_PAGE) {
        const uint64_t *pte = shared ? as_shared_pte_ro(as, page) : as_find_pte(as, page);
        if (!pte) return -1;
        if ((*pte & (PTE_PRESENT | PTE_USER)) != (PTE_PRESENT | PTE_USER)) return -1;
        if (write && !(*pte & PTE_WRITE)) return -1;
    }
    return 0;
}

int addrspace_shared_ok(void) {
    return g_shared_ok;
}

int addrspace_map_shared(addrspace_t *as, uint64_t va, const uint64_t *frames,
                         uint64_t count, uint32_t flags) {
    if (!as || !frames || count == 0) return -1;
    if (!g_shared_ok) {
        serial_puts("[AS] ERROR: shared region unavailable\n");
        return -1;
    }
    if (va & (AS_PAGE - 1)) return -1;
    if (count > (AS_SHARED_END - AS_SHARED_BASE) / AS_PAGE) return -1;
    uint64_t end = va + count * AS_PAGE;
    if (va < AS_SHARED_BASE || end > AS_SHARED_END || end <= va) return -1;

    /* The range must be completely unmapped */
    for (uint64_t page = va; page < end; page += AS_PAGE) {
        const uint64_t *pte = as_shared_pte_ro(as, page);
        if (pte && (*pte & PTE_PRESENT)) {
            serial_puts("[AS] ERROR: shared map over a mapped page at ");
            as_hex64(page);
            serial_puts("\n");
            return -1;
        }
    }

    uint64_t leaf = PTE_PRESENT | PTE_USER | PTE_SHARED |
                    ((flags & AS_MAP_WRITE) ? PTE_WRITE : 0);
    for (uint64_t i = 0; i < count; i++) {
        uint64_t frame = frames[i];
        uint64_t *pte = NULL;
        if ((frame & (AS_PAGE - 1)) == 0 && frame != 0 && frame + AS_PAGE <= IDENTITY_LIMIT) {
            pte = as_shared_pte(as, va + i * AS_PAGE, 1);
        }
        if (!pte) {
            serial_puts("[AS] ERROR: shared map failed at page ");
            as_dec(i);
            serial_puts("\n");
            addrspace_unmap_shared(as, va, i);
            return -1;
        }
        *pte = frame | leaf;
    }
    as->shared_pages += count;
    return 0;
}

void addrspace_unmap_shared(addrspace_t *as, uint64_t va, uint64_t count) {
    if (!as || count == 0) return;

    uint64_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    int active = ((cr3 & PTE_ADDR_MASK) == as->pml4_phys);

    for (uint64_t i = 0; i < count; i++) {
        uint64_t page = va + i * AS_PAGE;
        uint64_t *pte = as_shared_pte(as, page, 0);
        if (!pte || !(*pte & PTE_PRESENT)) continue;
        *pte = 0;
        if (as->shared_pages) as->shared_pages--;
        if (active) {
            __asm__ volatile("invlpg (%0)" : : "r"(page) : "memory");
        }
    }

    /* Give back page tables (and page directories) that became empty, so a
     * long-running process that maps and unmaps does not keep growing its
     * tables. The PDPT stays until the space is destroyed. */
    if (!as->shared_pdpt_phys || !as_va_in_shared(va)) return;
    int freed = 0;
    uint64_t *pdpt = (uint64_t *)as->shared_pdpt_phys;
    uint64_t end = va + count * AS_PAGE;
    for (uint64_t chunk = va & ~0x1FFFFFULL; chunk < end && chunk < AS_SHARED_END; chunk += 0x200000ULL) {
        uint64_t *pdpte = &pdpt[(chunk >> 30) & 0x1FF];
        if (!(*pdpte & PTE_PRESENT)) continue;
        uint64_t *pd = (uint64_t *)(*pdpte & PTE_ADDR_MASK);
        uint64_t *pde = &pd[(chunk >> 21) & 0x1FF];
        if (!(*pde & PTE_PRESENT)) continue;

        uint64_t *pt = (uint64_t *)(*pde & PTE_ADDR_MASK);
        int used = 0;
        for (int k = 0; k < 512 && !used; k++) used = (pt[k] & PTE_PRESENT) != 0;
        if (used) continue;
        *pde = 0;
        pmm_free_page((uint64_t)pt);
        freed = 1;

        int pd_used = 0;
        for (int k = 0; k < 512 && !pd_used; k++) pd_used = (pd[k] & PTE_PRESENT) != 0;
        if (!pd_used) {
            *pdpte = 0;
            pmm_free_page((uint64_t)pd);
        }
    }
    if (freed && active) {
        /* Drop cached paging-structure entries for the freed tables */
        __asm__ volatile("mov %%cr3, %%rax; mov %%rax, %%cr3" : : : "rax", "memory");
    }
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
