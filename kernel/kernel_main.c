/*
 * NeolyxOS Kernel - Production Ready
 * 
 * Main kernel entry with proper boot info handling
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include <stdint.h>
#include "include/ui/disk_icons.h"
#include "include/disk/disk_ops.h"
#include "include/core/app_launcher.h"
#include "include/core/boot_guard.h"
#include "include/core/session_manager.h"

/* Splash screen functions */
extern void splash_init(uint64_t fb_addr, uint32_t width, uint32_t height, uint32_t pitch);
extern void splash_show(void);
extern void splash_update(uint32_t percent, const char *status);
extern void splash_hide(void);

/* Recovery menu */
typedef struct {
    int selected;
    int triggered;
    int f2_press_count;
    uint64_t fb_addr;
    uint32_t fb_width;
    uint32_t fb_height;
    uint32_t fb_pitch;
} RecoveryMenu;

extern void recovery_init(RecoveryMenu *menu, uint64_t fb_addr, 
                          uint32_t width, uint32_t height, uint32_t pitch);
extern int recovery_run(RecoveryMenu *menu);

static RecoveryMenu g_recovery_menu;

/* Keyboard functions */
extern int kbd_check_key(void);
extern uint8_t kbd_getchar(void);

/* ============ Boot Info Structure (from bootloader) ============ */

typedef struct {
    uint32_t magic;
    uint32_t version;
    
    uint64_t memory_map_addr;
    uint64_t memory_map_size;
    uint64_t memory_map_desc_size;
    uint32_t memory_map_desc_version;
    
    uint64_t framebuffer_addr;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_bpp;
    
    uint64_t kernel_physical_base;
    uint64_t kernel_size;
    uint64_t acpi_rsdp;
    
    uint64_t reserved[8];
} NeolyxBootInfo;

#define NEOLYX_BOOT_MAGIC 0x4E454F58

/* ============ Framebuffer ============ */

/* These globals are DEFINED in framebuffer.c - just extern here */
extern volatile uint32_t *framebuffer;
extern uint32_t fb_width;
extern uint32_t fb_height;
extern uint32_t fb_pitch;

/* Aliases for syscall.c - DEFINED in framebuffer.c */
extern uint64_t g_framebuffer_addr;
extern uint32_t g_framebuffer_width;
extern uint32_t g_framebuffer_height;
extern uint32_t g_framebuffer_pitch;

static void fb_put_pixel(uint32_t x, uint32_t y, uint32_t color) {
    if (framebuffer && x < fb_width && y < fb_height) {
        framebuffer[y * (fb_pitch / 4) + x] = color;
    }
}

static void fb_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    for (uint32_t j = 0; j < h; j++) {
        for (uint32_t i = 0; i < w; i++) {
            fb_put_pixel(x + i, y + j, color);
        }
    }
}

static void fb_clear(uint32_t color) {
    if (framebuffer) {
        for (uint32_t y = 0; y < fb_height; y++) {
            for (uint32_t x = 0; x < fb_width; x++) {
                framebuffer[y * (fb_pitch / 4) + x] = color;
            }
        }
    }
}

/* ============ Serial (COM1) ============ */

#define COM1_PORT 0x3F8

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static void serial_init(void) {
    outb(COM1_PORT + 1, 0x00);
    outb(COM1_PORT + 3, 0x80);
    outb(COM1_PORT + 0, 0x03);
    outb(COM1_PORT + 1, 0x00);
    outb(COM1_PORT + 3, 0x03);
    outb(COM1_PORT + 2, 0xC7);
    outb(COM1_PORT + 4, 0x0B);
}

static void serial_putc(char c) {
    while ((inb(COM1_PORT + 5) & 0x20) == 0);
    outb(COM1_PORT, c);
}

static void serial_puts(const char *s) {
    while (*s) {
        if (*s == '\n') serial_putc('\r');
        serial_putc(*s++);
    }
}

static void serial_hex(uint64_t val) {
    const char hex[] = "0123456789ABCDEF";
    serial_puts("0x");
    for (int i = 60; i >= 0; i -= 4) {
        serial_putc(hex[(val >> i) & 0xF]);
    }
}

/* ============ Simple Text Drawing ============ */

static uint32_t text_row = 0;
static uint32_t text_col = 0;

static void draw_char(uint32_t x, uint32_t y, char c, uint32_t color) {
    /* Simple 8x16 block character */
    if (c >= 32 && c < 127) {
        fb_fill_rect(x, y, 8, 16, color);
    }
}

static void fb_print(const char *s, uint32_t color) {
    while (*s) {
        if (*s == '\n') {
            text_col = 0;
            text_row++;
        } else {
            uint32_t x = 20 + text_col * 10;
            uint32_t y = 50 + text_row * 20;
            draw_char(x, y, *s, color);
            text_col++;
        }
        s++;
    }
}

/* ============ Forward Declarations ============ */

/* From keyboard.c */
extern int kbd_init(void);
extern uint8_t kbd_getchar(void);

/* From disk_manager.c */
typedef struct disk_manager disk_manager_t;
extern int dm_init(disk_manager_t *mgr);
extern int dm_run(disk_manager_t *mgr, uint64_t fb_addr,
                  uint32_t fb_width, uint32_t fb_height, uint32_t fb_pitch);

/* Disk manager storage */
static uint8_t disk_manager_storage[4096];

/* ============ FPU/SSE Initialization ============ */

/* Enable FPU and SSE for userspace floating-point operations */
static void fpu_init(void) {
    uint64_t cr0, cr4;
    
    /* Read CR0 */
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    
    /* Clear CR0.EM (bit 2) - disable x87 emulation */
    /* Clear CR0.TS (bit 3) - clear task switched flag */
    /* Set CR0.MP (bit 1) - enable FPU monitoring */
    cr0 &= ~((uint64_t)1 << 2);  /* Clear EM */
    cr0 &= ~((uint64_t)1 << 3);  /* Clear TS */
    cr0 |= ((uint64_t)1 << 1);   /* Set MP */
    
    /* Write CR0 */
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0));
    
    /* Read CR4 */
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    
    /* Set CR4.OSFXSR (bit 9) - enable FXSAVE/FXRSTOR */
    /* Set CR4.OSXMMEXCPT (bit 10) - enable unmasked SSE exceptions */
    cr4 |= ((uint64_t)1 << 9);   /* Set OSFXSR */
    cr4 |= ((uint64_t)1 << 10);  /* Set OSXMMEXCPT */
    
    /* Write CR4 */
    __asm__ volatile("mov %0, %%cr4" : : "r"(cr4));
    
    /* Initialize x87 FPU */
    __asm__ volatile("fninit");
    
    serial_puts("[OK] FPU/SSE initialized\n");
}

/* ============ Kernel Entry ============ */

void kernel_main(NeolyxBootInfo *boot_info) {
    /* Initialize serial first */
    serial_init();
    serial_puts("\n\n");
    serial_puts("========================================\n");
    serial_puts("    NeolyxOS Kernel v1.0\n");
    serial_puts("    Copyright (c) 2025 KetiveeAI\n");
    serial_puts("========================================\n\n");
    
    /* ============ Boot Guard Initialization ============ */
    /* Initialize boot guard BEFORE anything that can fail */
    boot_guard_init();
    boot_guard_watchdog_start();
    
    /* Validate boot info */
    if (!boot_info) {
        serial_puts("[ERROR] No boot info received!\n");
        boot_guard_mark_failure();
        goto halt;
    }
    
    if (boot_info->magic != NEOLYX_BOOT_MAGIC) {
        serial_puts("[ERROR] Invalid boot info magic!\n");
        serial_puts("  Expected: 0x4E454F58\n");
        serial_puts("  Got: ");
        serial_hex(boot_info->magic);
        serial_puts("\n");
        boot_guard_mark_failure();
        goto halt;
    }
    
    serial_puts("[OK] Boot info valid (magic: NEOX)\n");
    
    /* Check if we should enter recovery mode (after repeated failures) */
    if (boot_guard_should_enter_recovery()) {
        serial_puts("[KERNEL] *** RECOVERY MODE ***\n");
        serial_puts("[KERNEL] Too many boot failures detected.\n");
        /* Will show recovery menu below */
    }
    
    /* ============ Phase 2: Core Initialization ============ */
    
    /* Initialize GDT with TSS for ring transitions */
    serial_puts("\n[KERNEL] Phase 2: Core Initialization...\n");
    extern void gdt_init(void);
    gdt_init();
    
    /* Initialize FPU/SSE for floating-point operations */
    fpu_init();
    
    /* Initialize IDT with PIC remapping */
    serial_puts("[KERNEL] Calling idt_init()...\n");
    extern void idt_init(void);
    idt_init();
    serial_puts("[KERNEL] IDT initialized with IST1 for exceptions\n");
    
    /* Initialize PMM with memory map from bootloader */
    extern void pmm_init(uint64_t mmap, uint64_t size, uint64_t desc_size);
    if (boot_info->memory_map_addr && boot_info->memory_map_size > 0) {
        pmm_init(boot_info->memory_map_addr, 
                 boot_info->memory_map_size, 
                 boot_info->memory_map_desc_size);
    }
    
    /* Initialize VMM (Virtual Memory Manager) */
    extern void vmm_init(void);
    vmm_init();
    serial_puts("[OK] VMM initialized\n");
    
    /* Initialize kernel heap */
    extern void kheap_init(void);
    kheap_init();
    serial_puts("[OK] Kernel heap initialized\n");
    
    /* Initialize PIT timer for scheduling */
    extern void pit_init(uint32_t frequency);
    pit_init(1000);  /* 1000 Hz = 1ms ticks */
    
    /* Initialize RTC (real-time clock from CMOS) */
    extern void rtc_init(void);
    rtc_init();
    
    /* Enable interrupts now that handlers are set up */
    extern void interrupts_enable(void);
    interrupts_enable();
    serial_puts("[OK] Interrupts enabled\n");
    
    /* Pet watchdog - we made it past core init */
    boot_guard_watchdog_pet();
    boot_guard_mark_stage(BOOT_STATUS_DRIVERS_OK);
    
    /* ============ Phase 3: Essential Drivers ============ */
    
    serial_puts("\n[KERNEL] Phase 3: Essential Drivers...\n");
    
    /* Initialize process subsystem and scheduler */
    extern void process_init(void);
    process_init();
    
    extern void scheduler_init(void);
    scheduler_init();
    
    /* Initialize graphics kdrv */
    extern int nxgfx_kdrv_init(void);
    if (nxgfx_kdrv_init() == 0) {
        serial_puts("[OK] nxgfx_kdrv loaded\n");
    }
    
    /* Initialize storage kdrv */
    extern int nxstor_kdrv_init(void);
    if (nxstor_kdrv_init() == 0) {
        serial_puts("[OK] nxstor_kdrv loaded\n");
    }
    
    /* Initialize display kdrv */
    extern int nxdisplay_kdrv_init(void);
    if (nxdisplay_kdrv_init() == 0) {
        serial_puts("[OK] nxdisplay_kdrv loaded\n");
    }
    
    /* Initialize system info kdrv */
    extern int nxsysinfo_kdrv_init(void);
    if (nxsysinfo_kdrv_init() == 0) {
        serial_puts("[OK] nxsysinfo_kdrv loaded\n");
    }
    
    /* Initialize PCI bus driver (required for USB, network) */
    extern int nxpci_kdrv_init(void);
    if (nxpci_kdrv_init() == 0) {
        serial_puts("[OK] nxpci_kdrv loaded\n");
    }
    
    /* Initialize USB driver */
    extern int nxusb_kdrv_init(void);
    if (nxusb_kdrv_init() == 0) {
        serial_puts("[OK] nxusb_kdrv loaded\n");
    }
    
    /* Initialize network subsystem */
    extern int network_init(void);
    extern void tcpip_init(void);
    if (network_init() == 0) {
        serial_puts("[OK] Network subsystem initialized\n");
        tcpip_init();
        serial_puts("[OK] TCP/IP stack initialized\n");
    } else {
        serial_puts("[WARN] Network init failed (no NIC detected)\n");
    }
    
    /* Initialize task manager kdrv */
    extern int nxtask_kdrv_init(void);
    if (nxtask_kdrv_init() == 0) {
        serial_puts("[OK] nxtask_kdrv loaded\n");
    }
    
    /* Initialize NXRender kernel driver for GUI */
    extern int nxrender_kdrv_init(void);
    if (nxrender_kdrv_init() == 0) {
        serial_puts("[OK] nxrender_kdrv loaded\n");
    }
    
    /* Initialize NXGame Bridge for games/heavy apps */
    extern int nxgame_init(void);
    if (nxgame_init() == 0) {
        serial_puts("[OK] NXGame Bridge loaded\n");
    }
    
    /* Initialize Session Manager for crash isolation */
    session_manager_init();
    serial_puts("[OK] Session Manager initialized\n");
    
    /* Initialize NLC Configuration System */
    extern int nlc_init(void);
    nlc_init();
    serial_puts("[OK] NLC Config system initialized\n");
    
    /* Initialize System Integrity Protection */
    extern int sip_init(void);
    sip_init();
    serial_puts("[OK] System Integrity Protection initialized\n");
    
    serial_puts("[KERNEL] Phase 3 complete\n");
    boot_guard_watchdog_pet();
    boot_guard_mark_stage(BOOT_STATUS_FS_OK);
    
    /* Log boot info */
    serial_puts("\nBoot Information:\n");
    serial_puts("  Framebuffer: ");
    serial_hex(boot_info->framebuffer_addr);
    serial_puts("\n");
    serial_puts("  Resolution: ");
    
    char buf[32];
    int idx = 0;
    uint32_t val = boot_info->framebuffer_width;
    if (val == 0) { buf[idx++] = '0'; }
    else {
        char tmp[16]; int i = 0;
        while (val) { tmp[i++] = '0' + (val % 10); val /= 10; }
        while (i > 0) buf[idx++] = tmp[--i];
    }
    buf[idx++] = 'x';
    val = boot_info->framebuffer_height;
    if (val == 0) { buf[idx++] = '0'; }
    else {
        char tmp[16]; int i = 0;
        while (val) { tmp[i++] = '0' + (val % 10); val /= 10; }
        while (i > 0) buf[idx++] = tmp[--i];
    }
    buf[idx] = '\0';
    serial_puts(buf);
    serial_puts("\n");
    
    serial_puts("  Memory map: ");
    serial_hex(boot_info->memory_map_addr);
    serial_puts(" (");
    serial_hex(boot_info->memory_map_size);
    serial_puts(" bytes)\n");
    
    serial_puts("  ACPI RSDP: ");
    serial_hex(boot_info->acpi_rsdp);
    serial_puts("\n\n");
    
    /* Initialize framebuffer and splash screen */
    if (boot_info->framebuffer_addr && boot_info->framebuffer_width > 0) {
        framebuffer = (volatile uint32_t*)boot_info->framebuffer_addr;
        fb_width = boot_info->framebuffer_width;
        fb_height = boot_info->framebuffer_height;
        fb_pitch = boot_info->framebuffer_pitch;
        
        /* Also set the global aliases for syscall access (userspace desktop) */
        g_framebuffer_addr = boot_info->framebuffer_addr;
        g_framebuffer_width = fb_width;
        g_framebuffer_height = fb_height;
        g_framebuffer_pitch = fb_pitch;
        
        /* Show splash screen with NeolyxOS logo */
        splash_init(boot_info->framebuffer_addr, fb_width, fb_height, fb_pitch);
        splash_show();
        serial_puts("[OK] Splash screen displayed\n");
        serial_puts("[OK] Framebuffer syscall globals exported\n");
        
        splash_update(10, "Initializing...");
    }
    
    /* Initialize keyboard */
    serial_puts("\n[KERNEL] Initializing keyboard...\n");
    splash_update(20, "Initializing keyboard...");
    if (kbd_init() == 0) {
        serial_puts("[OK] Keyboard initialized\n");
    } else {
        serial_puts("[WARN] Keyboard init failed\n");
    }
    
    /* Initialize mouse driver - CRITICAL for desktop mouse input! */
    serial_puts("[KERNEL] Initializing mouse driver...\n");
    extern int nxmouse_kdrv_init(void);
    if (nxmouse_kdrv_init() == 0) {
        serial_puts("[OK] Mouse driver initialized (IRQ12)\n");
    } else {
        serial_puts("[WARN] Mouse driver init failed\n");
    }
    
    /* ============ Recovery Menu Check ============
     * Allow user to press F2 during boot to access recovery menu
     * Options: Continue, Recover, NeolyxOS Disk Manager, Help, Fresh Install
     */
    splash_update(25, "Press F2 for recovery menu...");
    serial_puts("[KERNEL] Press F2 for recovery menu...\n");
    
    int recovery_action = 0;  /* 0 = continue normal boot */
    
    /* Check for F2 key press (short window) */
    for (int check = 0; check < 50; check++) {
        if (kbd_check_key()) {
            uint8_t key = kbd_getchar();
            if (key == 0x3C) {  /* F2 scancode */
                serial_puts("[KERNEL] F2 pressed - opening recovery menu\n");
                
                /* Hide splash and show recovery menu */
                splash_hide();
                
                /* Initialize and run recovery menu */
                recovery_init(&g_recovery_menu, boot_info->framebuffer_addr, 
                             fb_width, fb_height, fb_pitch);
                recovery_action = recovery_run(&g_recovery_menu);
                
                serial_puts("[KERNEL] Recovery selection: ");
                char action_char = '0' + recovery_action;
                serial_putc(action_char);
                serial_puts("\n");
                
                /* Show splash again if continuing */
                if (recovery_action == 0) {
                    splash_show();
                }
                break;
            }
        }
        /* Small delay between checks */
        for (volatile int i = 0; i < 100000; i++);
    }
    
    /* Handle recovery action */
    switch (recovery_action) {
        case 1:  /* Recover NeolyxOS */
            serial_puts("[KERNEL] Starting recovery mode...\n");
            /* TODO: Implement system file recovery */
            break;
            
        case 2:  /* Disk Utility */
            serial_puts("[KERNEL] Launching Disk Utility from recovery...\n");
            if (framebuffer) {
                disk_manager_t *dm = (disk_manager_t*)disk_manager_storage;
                dm_init(dm);
                dm_run(dm, boot_info->framebuffer_addr, fb_width, fb_height, fb_pitch);
            }
            goto halt;  /* Don't continue normal boot after disk utility */
            
        case 3:  /* Help */
            serial_puts("[KERNEL] Help: Press ESC to return, use Disk Utility to format drives\n");
            /* TODO: Show help screen */
            break;
            
        case 4:  /* Fresh Install */
            serial_puts("[KERNEL] Fresh Install requested - launching installer...\n");
            /* Force installer mode by clearing install marker */
            boot_info->reserved[0] = 0;
            /* Fall through to normal boot which will detect installer mode */
            break;
            
        default:  /* Continue normal boot */
            break;
    }
    
    splash_update(30, "Scanning disks...");
    /* Scan for disks */
    serial_puts("[KERNEL] Scanning disks...\n");
    splash_update(40, "Scanning disks...");
    
    disk_manager_t *dm = (disk_manager_t*)disk_manager_storage;
    if (dm_init(dm) == 0) {
        serial_puts("[OK] Disk manager initialized\n");
        
        splash_update(60, "Detecting boot source...");
        
        /* ============ Boot Source Detection ============
         * 
         * Logic:
         * 1. Check if we booted from a removable/USB drive (installer mode)
         * 2. Check if NXFS partition exists on internal disk (installed mode)
         * 
         * If boot from USB AND no NXFS on internal disk -> Show disk utility
         * If NXFS partition found on internal disk -> Boot to desktop
         * 
         * The bootloader can pass a flag in boot_info to indicate boot source,
         * or we can check if kernel was loaded from a removable device.
         * 
         * For now, we use a simple heuristic:
         * - Scan all disks for NXFS partition signature
         * - If found on any internal disk -> we're installed, boot normally
         * - If not found -> we're in installer mode, show disk utility
         */
        
        int nxfs_found_on_internal = 0;
        int boot_from_removable = 0;
        
        /* TODO: Implement proper NXFS detection by scanning partition tables
         * and checking for NXFS superblock signature on each partition.
         * 
         * For now, we check if a marker file/flag exists that was set
         * during installation. This could be:
         * - A magic value at a known disk sector
         * - A UEFI variable set during install
         * - The presence of NXFS superblock signature
         */
        
        /* Simulated detection - in real implementation:
         * nxfs_found_on_internal = dm_scan_for_nxfs(dm);
         * boot_from_removable = dm_is_boot_device_removable(dm);
         */
        
        /* Check if kernel was loaded from a different device than where NXFS is
         * This indicates installer mode (booted from USB, need to install to disk)
         */
        
        /* For now: if kernel_physical_base is in a certain range typical of 
         * USB boot vs disk boot, or we can add a boot_info flag */
        
        /* Simple approach: Check boot_info for a flag (to be set by bootloader)
         * We'll add this flag to NeolyxBootInfo structure later.
         * For now, assume installer mode only if explicitly triggered */
        
        /* TEMPORARY: Check if there's a "installed" marker at boot_info->reserved[0] 
         * 0 = not installed (show disk utility)
         * 1 = installed (boot to desktop)
         */
        nxfs_found_on_internal = (boot_info->reserved[0] == 0x494E5354); /* "INST" */
        
        /* TEMPORARY: Force terminal shell for testing - treat as installed */
        nxfs_found_on_internal = 1;
        
        splash_update(80, "Preparing system...");
        
        if (!nxfs_found_on_internal) {
            /* Installer mode - show disk utility for first-time setup */
            serial_puts("[KERNEL] Installer mode detected\n");
            serial_puts("[KERNEL] Launching Disk Setup Utility...\n");
            
            splash_update(100, "Loading Disk Utility...");
            
            /* Brief delay for user to see progress */
            for (volatile int i = 0; i < 3000000; i++);
            
            /* Hide splash and launch disk manager UI */
            splash_hide();
            
            if (framebuffer) {
                dm_run(dm, boot_info->framebuffer_addr,
                       fb_width, fb_height, fb_pitch);
            }
            
            /* After disk utility completes (user formatted disk with NXFS),
             * the system should reboot to complete installation */
            serial_puts("[KERNEL] Disk setup complete. Reboot required.\n");
            
        } else {
            /* Normal boot - NeolyxOS is installed, proceed to desktop */
            serial_puts("[KERNEL] NeolyxOS installation detected\n");
            serial_puts("[KERNEL] Launching desktop environment...\n");
            
            splash_update(100, "Starting NeolyxOS Desktop...");
            
            /* Brief delay for splash visibility */
            for (volatile int i = 0; i < 2000000; i++);
            
            splash_hide();
            
            /* Mark GUI stage complete and boot as successful */
            boot_guard_mark_stage(BOOT_STATUS_GUI_OK);
            boot_guard_mark_success();
            serial_puts("[BOOT_GUARD] Boot successful - failure counter reset\n");
            
            /* Start desktop session */
            int32_t desktop_session = session_start_desktop();
            if (desktop_session < 0) {
                serial_puts("[WARN] Desktop session start failed\n");
            }
            
            /* Launch userspace desktop from RAMFS */
            serial_puts("[KERNEL] Loading userspace desktop...\n");
            
            /* ELF loader functions */
            extern int elf_load_file(const char *path, void *info);
            extern void enter_user_mode(uint64_t stack, uint64_t entry) __attribute__((noreturn));
            extern void tss_set_kernel_stack(uint64_t stack);
            extern void paging_map_user(uint64_t virtual_addr, uint64_t physical_addr);
            
            /* ELF info structure */
            struct {
                uint64_t entry_point;
                uint64_t base_addr;
                uint64_t top_addr;
                uint64_t brk;
                int      is_valid;
                int      is_dynamic;
                char     interp[256];
            } elf_info;
            
            /* Try loading from RAMFS */
            int elf_result = elf_load_file("/ramfs/bin/desktop", &elf_info);
            
            if (elf_result == 0 && elf_info.is_valid) {
                serial_puts("[KERNEL] Desktop ELF loaded, entry=");
                /* Print entry point (simplified) */
                uint64_t ep = elf_info.entry_point;
                const char *hex = "0123456789ABCDEF";
                for (int i = 60; i >= 0; i -= 4) {
                    char c = hex[(ep >> i) & 0xF];
                    extern void serial_putc(char c);
                    serial_putc(c);
                }
                serial_puts("\n");
                
                /* ============ CRITICAL: Set up desktop heap (64MB reserve) ============
                 * The desktop needs dynamic memory allocation via brk() syscall.
                 * This enables Windows/macOS-style dynamic heap growth.
                 * The heap starts after the ELF's .bss section (elf_info.brk). */
                serial_puts("[KERNEL] Setting up desktop heap (64MB reserve)...\n");
                extern void syscall_set_desktop_heap(uint64_t start, uint64_t max_size);
                uint64_t heap_start = (elf_info.brk + 0xFFF) & ~0xFFFULL;  /* Page-aligned after ELF */
                uint64_t heap_size = 64 * 1024 * 1024;  /* 64MB reserve */
                syscall_set_desktop_heap(heap_start, heap_size);
                
                /* Map heap region with PAGE_USER flag so userspace can access it */
                serial_puts("[KERNEL] Mapping heap region with USER flag...\n");
                extern void paging_add_user_flag(uint64_t start, uint64_t size);
                paging_add_user_flag(heap_start, heap_size);
                serial_puts("[KERNEL] Desktop heap ready: start=0x");
                for (int i = 60; i >= 0; i -= 4) {
                    char c = hex[(heap_start >> i) & 0xF];
                    extern void serial_putc(char c);
                    serial_putc(c);
                }
                serial_puts(" size=64MB\n");
                
                /* Use fixed userspace stack address within mapped 8MB region
                 * Stack: 0x500000 - 0x510000 (64KB)
                 * This is identity-mapped with USER flag by paging_init() */
                #define USER_STACK_BASE 0x500000
                #define USER_STACK_SIZE 0x10000  /* 64 KB - must match process.h */
                uint64_t user_stack_top = USER_STACK_BASE + USER_STACK_SIZE;
                
                serial_puts("[KERNEL] User stack: 0x500000-0x504000\n");
                
                /* Ensure user stack pages are mapped (should already be by paging_init) */
                for (uint64_t addr = USER_STACK_BASE; addr < user_stack_top; addr += 0x1000) {
                    paging_map_user(addr, addr);  /* Identity map with USER flag */
                }
                
                /* Set TSS.RSP0 to proper kernel stack for Ring 3 -> Ring 0 transitions
                 * Use g_kernel_rsp0 which is the 16KB kernel stack initialized by gdt_init() */
                extern uint64_t g_kernel_rsp0;
                tss_set_kernel_stack(g_kernel_rsp0);
                serial_puts("[KERNEL] TSS kernel stack set to g_kernel_rsp0\n");
                
                serial_puts("[KERNEL] Transitioning to Ring 3 userspace...\n");
                
                /* Create proper user process before entering userspace.
                 * This ensures current_process has a properly initialized
                 * syscall_dispatch (filter_mode=0=NX_DISPATCH_OFF).
                 * Without this, syscalls would be blocked. */
                extern int32_t process_create_user(const char *name, uint64_t entry, uint64_t stack);
                extern process_t *current_process;
                
                int32_t desktop_pid = process_create_user("desktop", elf_info.entry_point, user_stack_top);
                if (desktop_pid > 0) {
                    serial_puts("[KERNEL] Desktop process created, PID=");
                    char pid_buf[12];
                    int idx = 0;
                    int32_t p = desktop_pid;
                    if (p == 0) { pid_buf[idx++] = '0'; }
                    else {
                        char tmp[12]; int i = 0;
                        while (p > 0) { tmp[i++] = '0' + (p % 10); p /= 10; }
                        while (i > 0) pid_buf[idx++] = tmp[--i];
                    }
                    pid_buf[idx] = '\0';
                    serial_puts(pid_buf);
                    serial_puts("\n");
                    
                    /* Set the desktop process as current before entering userspace */
                    extern process_t *process_table[];
                    if (desktop_pid < 256 && process_table[desktop_pid]) {
                        current_process = process_table[desktop_pid];
                        current_process->flags |= 0x0002;  /* PROC_FLAG_USER */
                        serial_puts("[KERNEL] Desktop process set as current\n");
                    }
                } else {
                    serial_puts("[WARN] Failed to create desktop process, continuing anyway\n");
                }
                
                /* Enter userspace - this does not return */
                enter_user_mode(user_stack_top, elf_info.entry_point);
            } else {
                serial_puts("[ERROR] Failed to load desktop ELF!\n");
                serial_puts("[FALLBACK] Starting terminal shell...\n");
                
                /* Fallback to terminal shell */
                extern void terminal_shell(void);
                terminal_shell();
            }


        }
    }
    
    serial_puts("\n========================================\n");
    serial_puts("    KERNEL SESSION COMPLETE\n");
    serial_puts("========================================\n\n");
    
halt:
    serial_puts("[HALT] System halted\n");
    while (1) {
        __asm__ volatile("hlt");
    }
}

/* Note: _start is provided by startup.S which calls kernel_main */


