/*
 * NeolyxOS x86_64 Paging Implementation
 * 
 * Implements 4-level paging with PAGE_USER support for userspace programs.
 * Creates fresh page tables using PMM, independent of UEFI's tables.
 * 
 * Memory Layout:
 *   0x000000 - 0x100000 : Identity mapped, kernel only (1MB)
 *   0x100000 - 0x200000 : Reserved, kernel only
 *   0x200000 - 0x400000 : Kernel heap/data, kernel only
 *   0x400000 - 0x500000 : User code (ELF at 0x400000), PAGE_USER
 *   0x500000 - 0x580000 : User stack, PAGE_USER
 *   0x580000 - 0x800000 : Extended kernel space
 *   Framebuffer         : Mapped with PAGE_USER for userspace rendering
 * 
 * Copyright (c) 2025 KetiveeAI.
 */

#include <stdint.h>
#include <stddef.h>

/* ============ Page Table Entry Flags ============ */

#define PAGE_PRESENT     (1ULL << 0)   /* Page is present in memory */
#define PAGE_WRITE       (1ULL << 1)   /* Page is writable */
#define PAGE_USER        (1ULL << 2)   /* User-mode access allowed */
#define PAGE_WRITETHROUGH (1ULL << 3)  /* Write-through caching */
#define PAGE_NOCACHE     (1ULL << 4)   /* Disable caching */
#define PAGE_ACCESSED    (1ULL << 5)   /* Page was accessed */
#define PAGE_DIRTY       (1ULL << 6)   /* Page was written to */
#define PAGE_LARGE       (1ULL << 7)   /* 2MB/1GB page (PS bit) */
#define PAGE_GLOBAL      (1ULL << 8)   /* Global page (not flushed on CR3 change) */
#define PAGE_NX          (1ULL << 63)  /* No execute (if EFER.NXE) */

#define PAGE_SIZE_4K     0x1000
#define PAGE_MASK        0xFFFFFFFFFFFFF000ULL  /* Mask for physical address */
#define INDEX_MASK       0x1FF                   /* 9-bit index */

/* ============ Page Table Types ============ */

typedef uint64_t pml4e_t;  /* PML4 Entry */
typedef uint64_t pdpte_t;  /* PDPT Entry */
typedef uint64_t pde_t;    /* PD Entry */
typedef uint64_t pte_t;    /* PT Entry */

/* ============ External Dependencies ============ */

extern void serial_puts(const char *s);
extern void serial_putc(char c);
extern uint64_t pmm_alloc_page(void);
extern void pmm_free_page(uint64_t page);

/* ============ Global State ============ */

static uint64_t g_pml4_phys = 0;   /* Physical address of our PML4 */
static uint64_t g_old_cr3 = 0;     /* Saved UEFI CR3 for fallback */
static int g_paging_active = 0;    /* Flag: are our page tables active? */

/* ============ Helper Functions ============ */

/* Zero a page (4KB) */
static void zero_page(uint64_t phys_addr) {
    uint64_t *ptr = (uint64_t *)phys_addr;
    for (int i = 0; i < 512; i++) {
        ptr[i] = 0;
    }
}

/* Print hex (for debugging) */
static void serial_hex64(uint64_t val) {
    char hex[17];
    for (int i = 15; i >= 0; i--) {
        int d = (val >> (i * 4)) & 0xF;
        hex[15 - i] = d < 10 ? '0' + d : 'A' + d - 10;
    }
    hex[16] = '\0';
    serial_puts(hex);
}

/* Extract indices from virtual address */
static inline uint64_t pml4_index(uint64_t virt) { return (virt >> 39) & INDEX_MASK; }
static inline uint64_t pdpt_index(uint64_t virt) { return (virt >> 30) & INDEX_MASK; }
static inline uint64_t pd_index(uint64_t virt)   { return (virt >> 21) & INDEX_MASK; }
static inline uint64_t pt_index(uint64_t virt)   { return (virt >> 12) & INDEX_MASK; }

/* ============ Page Table Management ============ */

/*
 * Allocate and zero a page for use as a page table.
 * Returns physical address.
 */
static uint64_t alloc_page_table(void) {
    uint64_t phys = pmm_alloc_page();
    if (phys == 0) {
        serial_puts("[PAGING] ERROR: Out of memory for page table!\n");
        return 0;
    }
    zero_page(phys);
    return phys;
}

/*
 * Get or create PDPT entry from PML4.
 * Returns physical address of PDPT.
 */
static uint64_t get_or_create_pdpt(uint64_t pml4_phys, uint64_t idx, uint64_t flags) {
    pml4e_t *pml4 = (pml4e_t *)pml4_phys;
    
    if (!(pml4[idx] & PAGE_PRESENT)) {
        uint64_t pdpt_phys = alloc_page_table();
        if (pdpt_phys == 0) return 0;
        pml4[idx] = pdpt_phys | PAGE_PRESENT | PAGE_WRITE | flags;
    }
    
    /* Add flags if not present (like PAGE_USER) */
    if (flags & PAGE_USER) {
        pml4[idx] |= PAGE_USER;
    }
    
    return pml4[idx] & PAGE_MASK;
}

/*
 * Get or create PD entry from PDPT.
 * Returns physical address of PD.
 * 
 * IMPORTANT: If PDPT entry is a 1GB huge page (PAGE_LARGE), we cannot
 * walk further and need to return an error.
 */
static uint64_t get_or_create_pd(uint64_t pdpt_phys, uint64_t idx, uint64_t flags) {
    pdpte_t *pdpt = (pdpte_t *)pdpt_phys;
    
    /* Check if this is a 1GB huge page */
    if ((pdpt[idx] & PAGE_PRESENT) && (pdpt[idx] & PAGE_LARGE)) {
        serial_puts("[PAGING] WARNING: Cannot split 1GB page at 0x");
        serial_hex64(pdpt[idx] & PAGE_MASK);
        serial_puts("\n");
        return 0;
    }
    
    if (!(pdpt[idx] & PAGE_PRESENT)) {
        uint64_t pd_phys = alloc_page_table();
        if (pd_phys == 0) return 0;
        pdpt[idx] = pd_phys | PAGE_PRESENT | PAGE_WRITE | flags;
    }
    
    if (flags & PAGE_USER) {
        pdpt[idx] |= PAGE_USER;
    }
    
    return pdpt[idx] & PAGE_MASK;
}

/*
 * Split a 2MB huge page into 512x 4KB pages.
 * This allows adding PAGE_USER to specific 4KB regions.
 * 
 * @param pd_entry Pointer to the PD entry containing the 2MB page
 * @return 0 on success, -1 on failure
 */
static int split_2mb_page(pde_t *pd_entry) {
    uint64_t huge_phys = *pd_entry & 0xFFFFFFFFFFE00000ULL;  /* 2MB aligned base */
    uint64_t orig_flags = *pd_entry & 0x1FF;  /* Lower 9 bits of flags (excl. PAGE_LARGE) */
    
    serial_puts("[PAGING] Splitting 2MB page at 0x");
    serial_hex64(huge_phys);
    serial_puts(" into 512x 4KB pages\n");
    
    /* Allocate new Page Table */
    uint64_t pt_phys = alloc_page_table();
    if (pt_phys == 0) {
        serial_puts("[PAGING] ERROR: Cannot allocate PT for splitting!\n");
        return -1;
    }
    
    /* Fill PT with 512x 4KB mappings covering the original 2MB region */
    pte_t *pt = (pte_t *)pt_phys;
    for (int i = 0; i < 512; i++) {
        /* Each 4KB page inherits flags from the 2MB page (minus PAGE_LARGE) */
        pt[i] = (huge_phys + (uint64_t)i * 0x1000) | (orig_flags & ~PAGE_LARGE) | PAGE_PRESENT | PAGE_WRITE;
    }
    
    /* Replace PD entry with pointer to new PT */
    *pd_entry = pt_phys | PAGE_PRESENT | PAGE_WRITE | (orig_flags & PAGE_USER);
    
    return 0;
}

/*
 * Get or create PT entry from PD.
 * Returns physical address of PT.
 * 
 * IMPORTANT: If PD entry is a 2MB huge page (PAGE_LARGE), we split it
 * into 4KB pages first to enable fine-grained PAGE_USER control.
 */
static uint64_t get_or_create_pt(uint64_t pd_phys, uint64_t idx, uint64_t flags) {
    pde_t *pd = (pde_t *)pd_phys;
    
    /* Check if this is a 2MB huge page */
    if ((pd[idx] & PAGE_PRESENT) && (pd[idx] & PAGE_LARGE)) {
        /* Split the 2MB page into 512x 4KB pages */
        if (split_2mb_page(&pd[idx]) != 0) {
            serial_puts("[PAGING] ERROR: Failed to split 2MB page!\n");
            return 0;
        }
        /* Now pd[idx] points to a PT, fall through to normal handling */
    }
    
    if (!(pd[idx] & PAGE_PRESENT)) {
        uint64_t pt_phys = alloc_page_table();
        if (pt_phys == 0) return 0;
        pd[idx] = pt_phys | PAGE_PRESENT | PAGE_WRITE | flags;
    }
    
    if (flags & PAGE_USER) {
        pd[idx] |= PAGE_USER;
    }
    
    return pd[idx] & PAGE_MASK;
}

/* ============ Public API ============ */

/*
 * Map a single 4KB page.
 * 
 * @param virt   Virtual address (will be page-aligned)
 * @param phys   Physical address (will be page-aligned)
 * @param flags  Page flags (PAGE_PRESENT, PAGE_WRITE, PAGE_USER, etc.)
 * @return 0 on success, -1 on failure
 */
int paging_map_page(uint64_t virt, uint64_t phys, uint64_t flags) {
    if (g_pml4_phys == 0) {
        serial_puts("[PAGING] ERROR: Page tables not initialized!\n");
        return -1;
    }
    
    /* Get indices */
    uint64_t i4 = pml4_index(virt);
    uint64_t i3 = pdpt_index(virt);
    uint64_t i2 = pd_index(virt);
    uint64_t i1 = pt_index(virt);
    
    /* Walk/create page table hierarchy */
    uint64_t user_flag = (flags & PAGE_USER) ? PAGE_USER : 0;
    
    uint64_t pdpt_phys = get_or_create_pdpt(g_pml4_phys, i4, user_flag);
    if (pdpt_phys == 0) return -1;
    
    uint64_t pd_phys = get_or_create_pd(pdpt_phys, i3, user_flag);
    if (pd_phys == 0) return -1;
    
    uint64_t pt_phys = get_or_create_pt(pd_phys, i2, user_flag);
    if (pt_phys == 0) return -1;
    
    /* Set the final page table entry */
    pte_t *pt = (pte_t *)pt_phys;
    pt[i1] = (phys & PAGE_MASK) | flags | PAGE_PRESENT;
    
    /* Invalidate TLB for this page if paging is active */
    if (g_paging_active) {
        __asm__ volatile("invlpg (%0)" : : "r"(virt) : "memory");
    }
    
    return 0;
}

/*
 * Map a contiguous range of physical memory.
 * 
 * @param virt_start  Starting virtual address
 * @param phys_start  Starting physical address
 * @param size        Size in bytes (will be page-aligned up)
 * @param flags       Page flags
 * @return 0 on success, -1 on failure
 */
int paging_map_range(uint64_t virt_start, uint64_t phys_start, uint64_t size, uint64_t flags) {
    uint64_t pages = (size + PAGE_SIZE_4K - 1) / PAGE_SIZE_4K;
    
    for (uint64_t i = 0; i < pages; i++) {
        uint64_t virt = virt_start + i * PAGE_SIZE_4K;
        uint64_t phys = phys_start + i * PAGE_SIZE_4K;
        
        if (paging_map_page(virt, phys, flags) != 0) {
            serial_puts("[PAGING] ERROR: Failed to map page at 0x");
            serial_hex64(virt);
            serial_puts("\n");
            return -1;
        }
    }
    
    return 0;
}

/*
 * Map a range with user access (convenience function).
 */
int paging_map_user_range(uint64_t virt_start, uint64_t phys_start, uint64_t size) {
    return paging_map_range(virt_start, phys_start, size, 
                            PAGE_PRESENT | PAGE_WRITE | PAGE_USER);
}

/*
 * Map a range for kernel only (convenience function).
 */
int paging_map_kernel_range(uint64_t virt_start, uint64_t phys_start, uint64_t size) {
    return paging_map_range(virt_start, phys_start, size,
                            PAGE_PRESENT | PAGE_WRITE);
}

/*
 * Load our page tables into CR3.
 */
void paging_activate(void) {
    if (g_pml4_phys == 0) {
        serial_puts("[PAGING] ERROR: Cannot activate - no page tables!\n");
        return;
    }
    
    /* Save current CR3 for fallback */
    __asm__ volatile("mov %%cr3, %0" : "=r"(g_old_cr3));
    
    serial_puts("[PAGING] Switching CR3: 0x");
    serial_hex64(g_old_cr3);
    serial_puts(" -> 0x");
    serial_hex64(g_pml4_phys);
    serial_puts("\n");
    
    /* Load new CR3 */
    __asm__ volatile("mov %0, %%cr3" : : "r"(g_pml4_phys) : "memory");
    
    g_paging_active = 1;
    serial_puts("[PAGING] New page tables active!\n");
}

/*
 * Restore UEFI page tables (fallback/recovery).
 */
void paging_restore_uefi(void) {
    if (g_old_cr3 != 0) {
        serial_puts("[PAGING] Restoring UEFI CR3: 0x");
        serial_hex64(g_old_cr3);
        serial_puts("\n");
        
        __asm__ volatile("mov %0, %%cr3" : : "r"(g_old_cr3) : "memory");
        g_paging_active = 0;
    }
}

/*
 * Add PAGE_USER flag to an existing page table entry.
 * Walks the cloned page table hierarchy and adds PAGE_USER at each level.
 * If a 2MB huge page is encountered, it is split into 4KB pages first.
 */
static int paging_add_user_flag_page(uint64_t virt) {
    uint64_t i4 = pml4_index(virt);
    uint64_t i3 = pdpt_index(virt);
    uint64_t i2 = pd_index(virt);
    uint64_t i1 = pt_index(virt);
    
    pml4e_t *pml4 = (pml4e_t *)g_pml4_phys;
    if (!(pml4[i4] & PAGE_PRESENT)) return -1;
    pml4[i4] |= PAGE_USER;
    
    pdpte_t *pdpt = (pdpte_t *)(pml4[i4] & PAGE_MASK);
    if (!(pdpt[i3] & PAGE_PRESENT)) return -1;
    pdpt[i3] |= PAGE_USER;
    
    /* Check for 1GB huge page - not supported yet */
    if (pdpt[i3] & PAGE_LARGE) {
        serial_puts("[PAGING] WARNING: 1GB page at ");
        serial_hex64(virt);
        serial_puts(" - cannot add PAGE_USER\n");
        return -1;
    }
    
    pde_t *pd = (pde_t *)(pdpt[i3] & PAGE_MASK);
    if (!(pd[i2] & PAGE_PRESENT)) return -1;
    
    /* Check for 2MB huge page - must split first! */
    if (pd[i2] & PAGE_LARGE) {
        if (split_2mb_page(&pd[i2]) != 0) {
            serial_puts("[PAGING] ERROR: Cannot split 2MB page at ");
            serial_hex64(virt);
            serial_puts("\n");
            return -1;
        }
    }
    
    /* Add PAGE_USER to PD entry */
    pd[i2] |= PAGE_USER;
    
    /* Now walk to PT and add PAGE_USER to the specific 4KB entry */
    pte_t *pt = (pte_t *)(pd[i2] & PAGE_MASK);
    if (pt[i1] & PAGE_PRESENT) {
        pt[i1] |= PAGE_USER;
    }
    
    return 0;
}

/*
 * Add PAGE_USER flag to a range of addresses.
 * 
 * Instead of trying to modify UEFI's 2MB page tables (which are read-only
 * or in protected memory), we create completely fresh 4KB mappings with
 * PAGE_USER flag for the specified region.
 * 
 * This replaces any existing mapping (including UEFI 2MB pages) with our
 * own 4KB pages that have proper permissions.
 */
int paging_add_user_flag(uint64_t virt_start, uint64_t size) {
    serial_puts("[PAGING] Creating fresh 4KB USER mappings for 0x");
    serial_hex64(virt_start);
    serial_puts(" - 0x");
    serial_hex64(virt_start + size);
    serial_puts("\n");
    
    /* Create fresh 4KB mappings (identity mapped) with PAGE_USER */
    for (uint64_t va = virt_start; va < virt_start + size; va += PAGE_SIZE_4K) {
        /* Identity map: virtual = physical (for ELF loaded at 0x400000) */
        if (paging_map_page(va, va, PAGE_PRESENT | PAGE_WRITE | PAGE_USER) != 0) {
            serial_puts("[PAGING] ERROR: Failed to map user page at 0x");
            serial_hex64(va);
            serial_puts("\n");
            return -1;
        }
    }
    
    serial_puts("[PAGING] User mappings created successfully\n");
    return 0;
}

/*
 * Map a single 2MB huge page (for kernel identity mapping).
 * 
 * Creates PML4 → PDPT → PD hierarchy if needed.
 * Sets PAGE_LARGE bit in PD entry (no PT level for 2MB pages).
 * 
 * @param virt  Virtual address (must be 2MB aligned)
 * @param phys  Physical address (must be 2MB aligned)
 * @param flags Page flags (PAGE_PRESENT, PAGE_WRITE, etc.)
 * @return 0 on success, -1 on failure
 */
int paging_map_2mb_page(uint64_t virt, uint64_t phys, uint64_t flags) {
    if (g_pml4_phys == 0) return -1;
    
    uint64_t i4 = pml4_index(virt);
    uint64_t i3 = pdpt_index(virt);
    uint64_t i2 = pd_index(virt);
    
    /* Get or create PDPT */
    uint64_t pdpt_phys = get_or_create_pdpt(g_pml4_phys, i4, 0);
    if (pdpt_phys == 0) return -1;
    
    /* Get or create PD */
    uint64_t pd_phys = get_or_create_pd(pdpt_phys, i3, 0);
    if (pd_phys == 0) return -1;
    
    /* Set PD entry as 2MB huge page (PAGE_LARGE) */
    pde_t *pd = (pde_t *)pd_phys;
    pd[i2] = (phys & 0xFFFFFFFFFFE00000ULL) | flags | PAGE_PRESENT | PAGE_LARGE;
    
    return 0;
}

/*
 * Initialize paging subsystem by creating FRESH kernel-owned page tables.
 * 
 * This is the correct approach used by FreeBSD, Linux, and production kernels:
 * 1. Allocate fresh PML4/PDPT/PD/PT from PMM (our memory, fully writable)
 * 2. Identity map 0-4GB with 2MB huge pages (kernel-only)
 * 3. Map userspace regions with 4KB pages (PAGE_USER)
 * 4. Switch CR3 to fresh tables
 * 
 * This avoids the fatal flaw of trying to modify UEFI's protected page tables.
 */
void paging_init(void) {
    serial_puts("[PAGING] Creating fresh page tables (kernel-owned)...\n");
    
    /* Step 1: Save UEFI CR3 for emergency fallback */
    __asm__ volatile("mov %%cr3, %0" : "=r"(g_old_cr3));
    serial_puts("[PAGING] UEFI CR3 saved: 0x");
    serial_hex64(g_old_cr3);
    serial_puts("\n");
    
    /* Step 2: Allocate fresh PML4 from PMM */
    g_pml4_phys = alloc_page_table();
    if (g_pml4_phys == 0) {
        serial_puts("[PAGING] FATAL: Cannot allocate PML4!\n");
        return;
    }
    serial_puts("[PAGING] Fresh PML4 at 0x");
    serial_hex64(g_pml4_phys);
    serial_puts("\n");
    
    /* Step 3: Identity map 0-4GB with 2MB huge pages (kernel-only) */
    serial_puts("[PAGING] Identity mapping 0-4GB with 2MB pages...\n");
    for (uint64_t addr = 0; addr < 4ULL * 1024 * 1024 * 1024; addr += 2 * 1024 * 1024) {
        if (paging_map_2mb_page(addr, addr, PAGE_PRESENT | PAGE_WRITE) != 0) {
            serial_puts("[PAGING] ERROR: Failed to map 2MB page at 0x");
            serial_hex64(addr);
            serial_puts("\n");
            return;
        }
    }
    serial_puts("[PAGING] Identity mapped 0-4GB with 2MB pages\n");
    
    /* Step 4: Map userspace regions with 4KB PAGE_USER pages */
    /*         This overwrites part of 2MB mapping with fine-grained 4KB */
    serial_puts("[PAGING] Mapping userspace with 4KB PAGE_USER pages...\n");
    
    /* User code region: 0x1000000 - 0x1200000 (2MB) */
    if (paging_add_user_flag(0x1000000, 0x200000) != 0) {
        serial_puts("[PAGING] ERROR: Failed to map user code region!\n");
        return;
    }
    
    /* User stack region: 0x1200000 - 0x1280000 (512KB) */
    if (paging_add_user_flag(0x1200000, 0x80000) != 0) {
        serial_puts("[PAGING] ERROR: Failed to map user stack region!\n");
        return;
    }
    
    serial_puts("[PAGING] User region 0x1000000-0x1280000 mapped with 4KB pages\n");
    serial_puts("[PAGING] Fresh page tables ready!\n");
}

/*
 * Map framebuffer for userspace access.
 * Call this after paging_init() but before paging_activate().
 */
int paging_map_framebuffer(uint64_t fb_addr, uint64_t fb_size) {
    serial_puts("[PAGING] Mapping framebuffer at 0x");
    serial_hex64(fb_addr);
    serial_puts(" (");
    
    /* Print size in KB */
    uint64_t kb = fb_size / 1024;
    char buf[16];
    int i = 0;
    do {
        buf[i++] = '0' + (kb % 10);
        kb /= 10;
    } while (kb > 0);
    while (i > 0) serial_putc(buf[--i]);
    
    serial_puts(" KB) with PAGE_USER...\n");
    
    /* Simple user-accessible mapping (no special caching) */
    return paging_map_range(fb_addr, fb_addr, fb_size,
        PAGE_PRESENT | PAGE_WRITE | PAGE_USER);
}


/*
 * Legacy API compatibility.
 * Maps a single user page (old interface).
 */
void paging_map_user(uint64_t virtual_addr, uint64_t physical_addr) {
    paging_map_page(virtual_addr, physical_addr, PAGE_PRESENT | PAGE_WRITE | PAGE_USER);
}

/*
 * Get the physical address of our PML4.
 */
uint64_t paging_get_pml4(void) {
    return g_pml4_phys;
}

/*
 * Use the current page tables (from boot manager).
 * 
 * This reads CR3 and sets g_pml4_phys so that paging_map_page() works
 * without calling paging_init() (which would recreate tables from scratch).
 * 
 * Use this when boot manager has already set up page tables and we just
 * need to extend them (e.g., map framebuffer for userspace).
 */
void paging_use_current_tables(void) {
    uint64_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    
    g_pml4_phys = cr3 & PAGE_MASK;
    g_old_cr3 = cr3;
    g_paging_active = 1;
    
    serial_puts("[PAGING] Using boot manager page tables, PML4 at 0x");
    serial_hex64(g_pml4_phys);
    serial_puts("\n");
}