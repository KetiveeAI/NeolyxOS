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
#include <video/splash.h>
#include <video/bootlog.h>
#include <core/boot_logfile.h>
#include <core/boot_image.h>

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

/*
 * Verbose (hardware diagnostics) boots only: hold the on-screen boot log for
 * about @seconds so it can be photographed before the desktop draws over it.
 * Uses the PIT uptime; gives up early if the timer is not running.
 */
static void boot_photo_pause(uint32_t seconds) {
    extern uint64_t pit_get_uptime_ms(void);
    extern void serial_puts(const char *s);
    /* Lines worth a photo: what was found, what was refused, what failed */
    static const char *const summary_patterns[] = {
        "WARNING", "ERROR", "FAIL", "failed", "[PS2]", "[USB]", "[STORAGE]",
        "[AHCI] Port", "[AHCI] Model", "[NVMe] Found", "Mounted", "[BOOT] Framebuffer",
        "[PCI] Found", "[E1000]", "Write refused",
    };
    serial_puts("[KERNEL] Verbose boot: showing the boot summary; the desktop starts in 15 seconds\n");
    bootlog_show_summary("NeolyxOS boot summary - take a photo; the desktop starts in 15 seconds",
                         summary_patterns,
                         (uint32_t)(sizeof(summary_patterns) / sizeof(summary_patterns[0])));
    uint64_t start = pit_get_uptime_ms();
    uint64_t spins = 0;
    while (pit_get_uptime_ms() - start < (uint64_t)seconds * 1000ULL) {
        __asm__ volatile("pause");
        if (++spins == 50000000ULL && pit_get_uptime_ms() == start) {
            serial_puts("[KERNEL] WARNING: timer not running, skipping the pause\n");
            return;
        }
    }
}

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
        klog("RAMFS mounted at /ramfs (/ramfs/bin/desktop when the bootloader loaded it)");
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
    splash_update(20, 0);
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
    splash_update(30, 0);
    serial_puts("[KERNEL] Fault tolerance ACTIVE - kernel never crashes\n");
    
    klog("Initializing high-level subsystems...");
    
    // Initialize system calls (safe to re-register)
    serial_puts("[KERNEL] Initializing syscalls...\n");
    init_system_calls();
    splash_update(40, 0);
    
    // Initialize process management
    serial_puts("[KERNEL] Initializing process management...\n");
    init_process_management();
    splash_update(50, 0);
    
    // Initialize runtime system
    serial_puts("[KERNEL] Initializing runtime...\n");
    init_runtime_system();
    splash_update(55, 0);
    
    // Hardware drivers - many may already be initialized, but this should be safe
    serial_puts("[KERNEL] Initializing hardware drivers...\n");
    init_hardware_drivers();
    splash_update(65, 0);
    
    // Storage and network - already done by boot manager, skip or make idempotent
    serial_puts("[KERNEL] Storage/Network already initialized by boot manager\n");

    // Initialize RAMFS for embedded userspace binaries
    serial_puts("[KERNEL] Initializing RAMFS...\n");
    extern void ramfs_init(void);
    extern int ramfs_mount_root(void);
    ramfs_init();
    int ramfs_ret = ramfs_mount_root();
    splash_update(75, 0);
    if (ramfs_ret == 0) {
        serial_puts("[KERNEL] RAMFS mounted at /ramfs\n");
    } else {
        serial_puts("[KERNEL] WARNING: RAMFS mount failed!\n");
    }

    /*
     * System Integrity Protection: from here on, userspace cannot change
     * /System, /EFI, /kernel.bin and the other protected paths. It is
     * loaded after the volumes are mounted and before the first user
     * process starts. Kernel-internal callers are not affected.
     */
    extern int sip_init(void);
    if (sip_init() != 0) {
        serial_puts("[KERNEL] WARNING: System Integrity Protection failed to initialize\n");
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
         * FAULT TOLERANCE: Register desktop service BEFORE launch so the
         * watchdog and fault router know about it.
         */
        extern int svc_register(const char *name);
        extern void svc_set_state(int id, int state);
        extern void svc_set_failed(int id);
        extern int watchdog_register(const char *name, uint32_t timeout_ms);
        extern void watchdog_enable(void);
        
        #define SVC_STATE_STARTING 1
        #define SVC_STATE_RUNNING  2
        
        int desktop_svc_id = svc_register("desktop");
        svc_set_state(desktop_svc_id, SVC_STATE_STARTING);
        (void)watchdog_register("desktop", 10000);
        watchdog_enable();
        serial_puts("[KERNEL] Desktop service registered (STARTING)\n");
        
        /*
         * Kernel-owned page tables: identity map 0-4GB (supervisor only).
         * The framebuffer gets PAGE_USER so the desktop compositor can
         * reach it; everything a process owns lives in its private window.
         */
        extern void paging_init(void);
        extern void paging_activate(void);
        extern int paging_map_framebuffer(uint64_t fb_addr, uint64_t fb_size);
        extern int addrspace_init(void);
        
        splash_update(80, 0);
        serial_puts("[KERNEL] Creating kernel page tables...\n");
        paging_init();
        uint64_t fb_size = (uint64_t)g_framebuffer_pitch * g_framebuffer_height;
        if (paging_map_framebuffer(g_framebuffer_addr, fb_size) != 0) {
            serial_puts("[KERNEL] WARNING: Failed to map framebuffer for userspace\n");
        }
        paging_activate();
        
        int launched = 0;
        if (addrspace_init() != 0) {
            serial_puts("[KERNEL] ERROR: per-process address spaces unavailable\n");
        } else {
            /* FPU/SSE + per-process FXSAVE areas (userspace uses SSE) */
            process_fpu_init();
            
            /* Ring 3 segments, TSS, SYSCALL/SYSRET */
            serial_puts("[KERNEL] Setting up GDT with Ring 3 segments...\n");
            extern void gdt_init(void);
            gdt_init();
            serial_puts("[KERNEL] Enabling SYSCALL instruction...\n");
            extern void syscall_msr_init(void);
            extern void syscall_enable(void);
            syscall_msr_init();
            syscall_enable();
            
            /*
             * The desktop shell is a userspace ELF on the boot volume
             * (\EFI\BOOT\desktop.elf), loaded into memory by the UEFI loader
             * and validated by boot_desktop_image_init(). It starts as a system
             * process in its own address space; programs it launches are
             * loaded from disk with sys_spawn and run alongside it.
             */
            const void *desktop_image = NULL;
            uint64_t desktop_image_size = 0;
            static const char *const desktop_argv[1] = { "desktop" };
            int32_t desktop_pid = -1;
            
            if (boot_desktop_image_get(&desktop_image, &desktop_image_size) != 0) {
                serial_puts("[KERNEL] ERROR: no desktop image (\\EFI\\BOOT\\desktop.elf missing or invalid)\n");
            } else {
                serial_puts("[KERNEL] Starting desktop process from boot image...\n");
                desktop_pid = process_spawn_image("desktop", desktop_image,
                                                  desktop_image_size,
                                                  1, desktop_argv,
                                                  PROC_FLAG_SYSTEM | PROC_FLAG_DETACHED);
            }
            if (desktop_pid > 0) {
                svc_set_state(desktop_svc_id, SVC_STATE_RUNNING);
                serial_puts("[KERNEL] Desktop service state: RUNNING\n");
                /* Leave this boot's log on the boot volume for machines
                 * without a serial port (\EFI\NEOLYX\boot.log) */
                (void)boot_logfile_save("desktop started");
                /* The desktop owns the screen from here; the boot log stays
                 * in memory for the crash screen. */
                if (bootlog_mirror_active()) {
                    boot_photo_pause(15);
                    bootlog_set_mirror(0);
                    serial_puts("[KERNEL] Desktop starting: on-screen boot log ends here\n");
                }
                splash_update(95, 0);
                launched = 1;
            } else {
                serial_puts("[KERNEL] ERROR: Desktop process could not be created\n");
                svc_set_failed(desktop_svc_id);
            }
        }
        
        if (launched) {
            /* Wired networking: the network service thread brings up the
             * e1000 interfaces and runs DHCP once the scheduler starts, so
             * a missing cable or DHCP server never delays the desktop. */
            extern int network_service_start(void);
            if (network_service_start() < 0) {
                serial_puts("[KERNEL] WARNING: network service not started; no networking\n");
            }

            /* The scheduler needs a steady tick: program the PIT for ~1000Hz
             * (sleep(), time slices and SYS_TIME are all in PIT ticks).
             * Firmware leaves it at an unknown rate. */
            extern void pit_init(void);
            pit_init();
            splash_update(100, 0);
            
            /* The boot context becomes the idle thread; never returns */
            scheduler_run();
        }
        
        /*
         * If we reach here the desktop could not start.
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