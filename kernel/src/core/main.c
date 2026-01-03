// kernel/main.c
/*
 * NeolyxOS Full Kernel Entry
 * 
 * ARCHITECTURE NOTE:
 * This is the FULL KERNEL that is launched by the Boot Manager.
 * The boot sequence is:
 *   1. UEFI Bootloader (boot/BOOTX64.EFI) loads kernel.bin
 *   2. kernel_main() in minimal_main.c runs the Boot Manager UI
 *   3. When "Boot NeolyxOS" is selected, nx_kernel_start() is called
 *   4. This file (main.c) initializes all subsystems and runs the OS
 *
 * DO NOT rename nx_kernel_start() - it is called by the Boot Manager!
 *
 * Copyright (c) 2025 KetiveeAI
 */
#include <core/kernel.h>
#include <video/video.h>
#include <drivers/drivers.h>
#include <network/network.h>
#include <storage/storage.h>
#include <utils/log.h>
#include <io/keyboard.h>
#include <core/syscall.h>
#include <core/process.h>
#include <core/runtime.h>
#include <utils/string.h>

/* ============ Framebuffer Globals for Syscalls ============ */
/* 
 * These globals are used by sys_fb_* syscalls in syscall.c
 * They provide userspace access to the framebuffer for the desktop shell.
 * DEFINED in kernel_main.c - only extern declare here to avoid duplicates.
 */
extern uint64_t g_framebuffer_addr;
extern uint32_t g_framebuffer_width;
extern uint32_t g_framebuffer_height;
extern uint32_t g_framebuffer_pitch;

// Architecture-specific includes
extern void gdt_init(void);
extern void idt_init(void);
extern void paging_init(void);
extern void heap_init(uint64_t start_addr, size_t size);

// Timer includes
extern void timer_init(uint32_t frequency);

void init_memory(void) {
    klog("Initializing memory management...");
    
    // Initialize paging
    paging_init();
    klog("Paging initialized");
    
    // Initialize heap (start at 1MB, 16MB size)
    heap_init(0x100000, 16 * 1024 * 1024);
    klog("Heap initialized");
    
    klog("Memory management initialized");
}

void init_architecture(void) {
    klog("Initializing x86_64 architecture...");
    
    // Initialize GDT
    gdt_init();
    klog("GDT initialized");
    
    // Initialize IDT
    idt_init();
    klog("IDT initialized");
    
    // Initialize timer
    timer_init(100); // 100 Hz
    klog("Timer initialized");
    
    klog("Architecture initialization complete");
}

void init_system_calls(void) {
    klog("Initializing system calls...");
    syscall_init();
    klog("System calls initialized");
}

void init_process_management(void) {
    klog("Initializing process management...");
    process_init();
    klog("Process management initialized");
}

void init_runtime_system(void) {
    klog("Initializing runtime system...");
    runtime_init();
    klog("Runtime system initialized");
}

void init_network_subsystem(void) {
    klog("Initializing network subsystem...");
    if (network_init() == 0) {
        klog("Network subsystem ready");
    } else {
        klog("Warning: Network subsystem initialization failed");
    }
}

void init_storage_subsystem(void) {
    klog("Initializing storage subsystem...");
    if (storage_init() == 0) {
        klog("Storage subsystem ready");
    } else {
        klog("Warning: Storage subsystem initialization failed");
    }
    
    /* Initialize VFS layer */
    extern void vfs_init(void);
    vfs_init();
    klog("VFS initialized");
    
    /* Register NXFS filesystem */
    extern void nxfs_init(void);
    nxfs_init();
    klog("NXFS registered with VFS");
    
    /* Initialize and mount ramfs for embedded binaries */
    extern void ramfs_init(void);
    extern int ramfs_mount_root(void);
    ramfs_init();
    if (ramfs_mount_root() == 0) {
        klog("RAMFS mounted at /ramfs (provides /ramfs/bin/desktop)");
    } else {
        klog("Warning: RAMFS mount failed");
    }
}

void init_hardware_drivers(void) {
    klog("Initializing hardware drivers...");
    if (kernel_drivers_init() == 0) {
        klog("Hardware drivers ready");
    } else {
        klog("Warning: Hardware drivers initialization failed");
    }
    
    /* Phase 3: Initialize kdrv drivers */
    klog("Loading kdrv drivers...");
    
    extern int nxgfx_kdrv_init(void);
    if (nxgfx_kdrv_init() == 0) {
        klog("  nxgfx_kdrv loaded");
    }
    
    extern int nxstor_kdrv_init(void);
    if (nxstor_kdrv_init() == 0) {
        klog("  nxstor_kdrv loaded");
    }
    
    extern int nxdisplay_kdrv_init(void);
    if (nxdisplay_kdrv_init() == 0) {
        klog("  nxdisplay_kdrv loaded");
    }
    
    extern int nxsysinfo_kdrv_init(void);
    if (nxsysinfo_kdrv_init() == 0) {
        klog("  nxsysinfo_kdrv loaded");
    }
    
    /* Mouse driver for desktop input */
    extern int nxmouse_kdrv_init(void);
    if (nxmouse_kdrv_init() == 0) {
        klog("  nxmouse_kdrv loaded (IRQ12)");
    }
    
    klog("All kdrv drivers loaded");
}

void halt(void) {
    klog("System halted. Press any key to restart...");
    while(1) {
        __asm__ volatile("hlt");
    }
}

/*
 * nx_kernel_start - Full NeolyxOS Kernel Entry Point
 *
 * Called by the Boot Manager (minimal_main.c) when user selects "Boot NeolyxOS"
 * or after installation completes.
 * 
 * NOTE: When called from installer path, GDT/IDT/PMM are already initialized
 * by minimal_main.c. We skip re-initialization to avoid crashes.
 */
void nx_kernel_start(void) {
    extern void serial_puts(const char *s);
    serial_puts("[KERNEL] nx_kernel_start() called\n");
    
    /* 
     * Skip video/architecture/memory init - already done by boot manager!
     * minimal_main.c already initialized:
     *   - GDT/IDT (init_architecture)
     *   - PMM (pmm_init_from_boot_info)
     *   - Heap (heap_init)
     *   - Framebuffer
     * 
     * We only need to initialize higher-level subsystems and launch desktop.
     */
    
    serial_puts("[KERNEL] Boot manager pre-initialized core subsystems\n");
    
    // NOTE: GDT with Ring 3 user segments is needed for userspace desktop
    // But calling gdt_init() here crashes - needs investigation
    // For now, kernel shell works. Userspace desktop is a TODO.
    // TODO: Fix GDT reload to support Ring 3
    serial_puts("[KERNEL] Using boot GDT (Ring 3 transition TODO)\n");
    
    // Initialize logging
    serial_puts("[KERNEL] Initializing logging...\n");
    klog_init("../bin/kernel.log");
    klog("Kernel log started");
    
    // ============ FAULT TOLERANCE SUBSYSTEM ============
    // Initialize FIRST - catches faults in other init functions
    // Kernel never crashes, never auto-reboots after this point
    serial_puts("[KERNEL] Initializing fault tolerance subsystem...\n");
    extern void emergency_stack_init(void);
    extern void fault_router_init(void);
    extern void reboot_guard_init(void);
    extern void watchdog_init(void);
    
    emergency_stack_init();   // Pre-allocated stack for stack corruption
    fault_router_init();      // Routes faults by origin
    reboot_guard_init();      // Blocks software reboots
    watchdog_init();          // Passive kernel-owned monitoring
    serial_puts("[KERNEL] Fault tolerance ACTIVE - kernel never crashes\n");
    
    klog("Initializing high-level subsystems...");
    
    // Initialize system calls (safe to re-register)
    serial_puts("[KERNEL] Initializing syscalls...\n");
    init_system_calls();
    
    // Initialize process management
    serial_puts("[KERNEL] Initializing process management...\n");
    init_process_management();
    
    // Initialize runtime system
    serial_puts("[KERNEL] Initializing runtime...\n");
    init_runtime_system();
    
    // Hardware drivers - many may already be initialized, but this should be safe
    serial_puts("[KERNEL] Initializing hardware drivers...\n");
    init_hardware_drivers();
    
    // Storage and network - already done by boot manager, skip or make idempotent
    serial_puts("[KERNEL] Storage/Network already initialized by boot manager\n");

    // Initialize RAMFS for embedded userspace binaries
    serial_puts("[KERNEL] Initializing RAMFS...\n");
    extern void ramfs_init(void);
    extern int ramfs_mount_root(void);
    ramfs_init();
    int ramfs_ret = ramfs_mount_root();
    if (ramfs_ret == 0) {
        serial_puts("[KERNEL] RAMFS mounted at /ramfs\n");
    } else {
        serial_puts("[KERNEL] WARNING: RAMFS mount failed!\n");
    }

    serial_puts("[KERNEL] All subsystems ready!\n");
    klog("Kernel loaded successfully!");
    klog("System ready.");
    
    // Show available features
    klog("Available subsystems:");
    klog("  - Network (WiFi/Ethernet)");
    klog("  - Storage (Disk/USB)");
    klog("  - Input (Keyboard/Mouse)");
    klog("  - Graphics (VGA/GPU)");
    klog("  - Audio");
    klog("  - USB");
    klog("  - PCI");
    klog("  - Process Management");
    klog("  - System Calls");
    klog("  - Runtime Support (C#, C++, Objective-C, Rust)");
    
    /* ============ Launch Desktop Shell ============ */
    
    /*
     * Launch the macOS-style desktop shell with dock and menu bar.
     * Uses desktop_shell.c implementation.
     */
    /* Get framebuffer from minimal_main.c globals */
    extern volatile uint32_t *boot_framebuffer;
    extern uint32_t boot_fb_width, boot_fb_height, boot_fb_pitch;
    
    serial_puts("[KERNEL] Launching shell environment...\n");
    klog("Starting terminal shell");
    
    /*
     * ARCHITECTURE NOTE:
     * Desktop is NOT in kernel - it runs in USERSPACE via syscalls.
     * The kernel provides framebuffer syscalls (100-106), and the
     * userspace desktop (/desktop/shell/) uses them.
     * 
     * DEBUGGING: Run in Ring 0 first to verify ELF loading works.
     * Ring 3 will be added once we fix paging properly.
     */
    
    if (boot_framebuffer && boot_fb_width > 0 && boot_fb_height > 0) {
        /* Set global framebuffer values for syscall access */
        g_framebuffer_addr = (uint64_t)boot_framebuffer;
        g_framebuffer_width = boot_fb_width;
        g_framebuffer_height = boot_fb_height;
        g_framebuffer_pitch = boot_fb_pitch;
        serial_puts("[KERNEL] Framebuffer syscall globals exported\n");
        
        /* 
         * Launch USERSPACE Desktop via ELF loading
         * 
         * The desktop shell uses syscalls for framebuffer, input, and
         * other kernel services. It runs in Ring 3 (userspace).
         */
        
        serial_puts("[KERNEL] Preparing userspace environment...\n");
        
        /* 
         * CRITICAL: Do NOT modify UEFI page tables!
         * UEFI/boot manager already set up:
         *   - Identity mapping for kernel memory
         *   - Framebuffer at 0xC0000000 (already mapped)
         *   - User code region at 0x400000 (already mapped)
         * 
         * Modifying UEFI page tables at 0xBF801000 causes triple fault.
         * The framebuffer is accessed through kernel syscalls, so userspace
         * PAGE_USER flags are not strictly required.
         */
        serial_puts("[KERNEL] Will switch to kernel-owned page tables\n");
        
        /*
         * FAULT TOLERANCE: Register desktop service BEFORE ELF load
         * If ELF load fails, watchdog can act
         */
        extern int svc_register(const char *name);
        extern void svc_set_state(int id, int state);
        extern int watchdog_register(const char *name, uint32_t timeout_ms);
        extern void watchdog_enable(void);
        
        #define SVC_STATE_STARTING 1
        #define SVC_STATE_RUNNING  2
        
        int desktop_svc_id = svc_register("desktop");
        svc_set_state(desktop_svc_id, SVC_STATE_STARTING);
        (void)watchdog_register("desktop", 10000);
        watchdog_enable();
        serial_puts("[KERNEL] Desktop service registered (STARTING)\n");
        
        serial_puts("[KERNEL] Loading desktop from embedded ELF data...\n");
        
        /* Include the embedded desktop ELF data */
        #include "../fs/desktop_elf_data.h"
        
        /* Direct ELF loader */
        extern int elf_load(const void *data, void *info);
        
        struct {
            uint64_t entry_point;
            uint64_t base_addr;
            uint64_t top_addr;
            uint64_t brk;
            int      is_valid;
            int      is_dynamic;
            char     interp[256];
        } elf_info;
        
        serial_puts("[KERNEL] Desktop ELF size: ");
        uint64_t sz = sizeof(desktop_elf_data);
        char szstr[16];
        int idx = 0;
        if (sz == 0) { szstr[idx++] = '0'; }
        else {
            uint64_t tmp = sz;
            int digits = 0;
            while (tmp > 0) { digits++; tmp /= 10; }
            idx = digits;
            szstr[idx] = '\0';
            while (sz > 0) { szstr[--idx] = '0' + (sz % 10); sz /= 10; }
            idx = digits;
        }
        szstr[idx] = '\0';
        serial_puts(szstr);
        serial_puts(" bytes\n");
        
        int elf_result = elf_load(desktop_elf_data, &elf_info);
        
        if (elf_result == 0 && elf_info.is_valid && elf_info.entry_point != 0) {
            serial_puts("[KERNEL] Desktop ELF loaded successfully\n");
            
            /* Mark service as RUNNING before jump */
            svc_set_state(desktop_svc_id, SVC_STATE_RUNNING);
            serial_puts("[KERNEL] Desktop service state: RUNNING\n");
            
            /*
             * Set up proper kernel page tables for Ring 3 userspace.
             * paging_init() now CLONES UEFI's PML4, preserving all mappings
             * including IDT/ISR handlers so interrupts work after CR3 switch.
             */
            serial_puts("[KERNEL] Initializing kernel page tables (cloning UEFI)...\n");
            extern void paging_init(void);
            extern int paging_map_framebuffer(uint64_t fb_addr, uint64_t fb_size);
            extern void paging_activate(void);
            extern int paging_add_user_flag(uint64_t virt_start, uint64_t size);
            
            paging_init();  /* Clones UEFI PML4, preserves ALL mappings */
            
            /* 
             * Add PAGE_USER flags to user regions.
             * The ELF is loaded at 0x400000, stack at 0x600000+.
             * UEFI uses 2MB huge pages, so paging_add_user_flag will
             * split them into 4KB pages first.
             */
            serial_puts("[KERNEL] Adding PAGE_USER to user code region (0x1000000-0x1200000)...\n");
            if (paging_add_user_flag(0x1000000, 0x200000) != 0) {
                serial_puts("[KERNEL] WARNING: Failed to set PAGE_USER for code region\n");
            }
            
            serial_puts("[KERNEL] Adding PAGE_USER to user stack region (0x1200000-0x1280000)...\n");
            if (paging_add_user_flag(0x1200000, 0x80000) != 0) {
                serial_puts("[KERNEL] WARNING: Failed to set PAGE_USER for stack region\n");
            }
            
            /* Map framebuffer with PAGE_USER for userspace desktop access */
            serial_puts("[KERNEL] Mapping framebuffer for userspace access...\n");
            uint64_t fb_size = (uint64_t)g_framebuffer_pitch * g_framebuffer_height;
            if (paging_map_framebuffer(g_framebuffer_addr, fb_size) != 0) {
                serial_puts("[KERNEL] WARNING: Failed to map framebuffer for userspace\n");
            }
            
            /* Activate our page tables (switch CR3) */
            paging_activate();
            serial_puts("[KERNEL] Kernel page tables active (UEFI mappings preserved)\n");
            
            /* Framebuffer mapping verified - no debug drawing needed */
            
            /*
             * Initialize GDT with Ring 3 segments and TSS
             */
            serial_puts("[KERNEL] Setting up GDT with Ring 3 segments...\n");
            extern void gdt_init(void);
            gdt_init();
            
            /* Set up SYSCALL/SYSRET MSRs */
            serial_puts("[KERNEL] Enabling SYSCALL instruction...\n");
            extern void syscall_msr_init(void);
            extern void syscall_enable(void);
            syscall_msr_init();
            syscall_enable();
            
            /* Set up desktop heap: starts after ELF ends, 64MB reserve 
             * This enables Windows/macOS-style dynamic heap via brk() syscall */
            serial_puts("[KERNEL] Setting up desktop heap (64MB reserve)...\n");
            extern void syscall_set_desktop_heap(uint64_t start, uint64_t max_size);
            uint64_t heap_start = (elf_info.brk + 0x1000) & ~0xFFF;  /* Page-aligned after ELF */
            uint64_t heap_size = 64 * 1024 * 1024;  /* 64MB reserve */
            syscall_set_desktop_heap(heap_start, heap_size);
            
            /* Map heap region with PAGE_USER */
            serial_puts("[KERNEL] Mapping heap region for userspace access...\n");
            if (paging_add_user_flag(heap_start, heap_size) != 0) {
                serial_puts("[KERNEL] WARNING: Failed to map heap with PAGE_USER\n");
            }
            
            /* User stack at top of 18MB-18.5MB region */
            uint64_t user_stack_top = 0x127F000;
            
            serial_puts("[KERNEL] Jumping to userspace (Ring 3)...\n");
            
            extern void enter_user_mode(uint64_t stack, uint64_t entry) __attribute__((noreturn));
            enter_user_mode(user_stack_top, elf_info.entry_point);
            
            /* UNREACHABLE - enter_user_mode uses iretq and never returns */
        } else {
            serial_puts("[KERNEL] ERROR: Desktop ELF load failed\n");
            /* Mark service as failed so fault_router knows */
            extern void svc_set_failed(int id);
            svc_set_failed(desktop_svc_id);
        }
        
        /*
         * If we reach here: ELF load failed or enter_user_mode returned (BUG)
         * Enter SAFE MODE - keep serial shell alive
         */
        serial_puts("[KERNEL] Entering SAFE MODE - serial shell available\n");
        extern void enter_safe_mode(void);
        enter_safe_mode();
    }
    
    /* Fallback: Text-based serial shell if desktop fails */
    serial_puts("\nNeolyxOS Serial Shell (fallback mode)\n");
    serial_puts("Type 'help' for commands\n\n");
    
    char cmd[128];
    while (1) {
        serial_puts("neolyx> ");
        int pos = 0;
        while (1) {
            char c = keyboard_getchar();
            if (c == '\n' || c == '\r') {
                serial_puts("\n");
                cmd[pos] = 0;
                break;
            } else if (c == '\b' && pos > 0) {
                pos--;
                serial_puts("\b \b");
            } else if (c >= 32 && c < 127 && pos < 127) {
                cmd[pos++] = c;
                /* Echo char to serial */
                char tmp[2] = {c, 0};
                serial_puts(tmp);
            }
        }
        
        if (strcmp(cmd, "help") == 0) {
            serial_puts("Commands: help, info, exec <file>, exit\n");
        } else if (strcmp(cmd, "info") == 0) {
            serial_puts("NeolyxOS Kernel v1.0.0 (serial fallback)\n");
        } else if (strncmp(cmd, "exec ", 5) == 0) {
            process_exec(cmd + 5, NULL, NULL);
        } else if (strcmp(cmd, "exit") == 0) {
            serial_puts("Shutting down...\n");
            runtime_cleanup();
            halt();
        } else if (strlen(cmd) > 0) {
            serial_puts("Unknown: ");
            serial_puts(cmd);
            serial_puts("\n");
        }
    }
} 