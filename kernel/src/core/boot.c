/*
 * NeolyxOS Boot Sequence Implementation
 * 
 * Initializes all drivers in correct order.
 * 
 * Copyright (c) 2025 KetiveeAI.
 */

#include "core/boot.h"
#include "core/power.h"
#include "core/sysconfig.h"
#include "drivers/nxaudio.h"
#include "drivers/gpu.h"
#include "drivers/usb.h"
#include "drivers/wifi.h"
#include "drivers/e1000.h"
#include "drivers/bluetooth.h"
#include "drivers/print.h"
#include "drivers/ahci.h"
#include "net/network.h"
#include "net/tcpip.h"
#include "mm/pmm.h"
#include "mm/kheap.h"
#include "fs/vfs.h"
#include "fs/fs_init.h"

/* Platform detection (early boot) */
extern void platform_detect(void);
extern void platform_init(void);
extern int bsp_init(void);  /* Bootstrap processor init */

/* ============ External Dependencies ============ */

extern void serial_puts(const char *s);
extern void serial_putc(char c);
extern void serial_init(void);
extern void fb_clear(uint32_t color);
extern uint32_t fb_width, fb_height;
extern volatile uint32_t *framebuffer;

/* Power and sysconfig functions */
extern void power_print_status(void);
extern int sysconfig_get_profile(void);

/* Font functions */
extern void font_init(void);
extern void font_draw_centered(void *font, int y, const char *text, uint32_t color);
extern void *font_get_system(uint32_t size);

/* Security and services */
extern void firewall_init(void);
extern int app_launcher_init(void);
extern int session_manager_init(void);

/* ============ State ============ */

static boot_status_t boot_status = {0};
static boot_progress_fn progress_callback = NULL;
/* boot_start_time reserved for future timing metrics */
static uint32_t boot_start_time __attribute__((unused)) = 0;

/* ============ Colors ============ */

#define COLOR_BG        0x0A0A14
#define COLOR_TEXT      0xFFFFFF
#define COLOR_DIM       0x888888
#define COLOR_ACCENT    0xE94560
#define COLOR_SUCCESS   0x4CAF50
#define COLOR_ERROR     0xF44336

/* ============ Boot Progress Display ============ */

static void boot_draw_progress(const char *stage, const char *driver, int percent) {
    (void)stage;  /* Used for stage name display - reserved */
    if (!framebuffer || fb_width == 0) return;
    
    /* Draw progress bar */
    int bar_y = fb_height - 60;
    int bar_w = fb_width - 100;
    int bar_h = 4;
    int bar_x = 50;
    
    /* Background */
    for (int y = bar_y; y < bar_y + bar_h; y++) {
        for (int x = bar_x; x < bar_x + bar_w; x++) {
            framebuffer[y * (fb_width) + x] = 0x333333;
        }
    }
    
    /* Progress */
    int progress_w = (bar_w * percent) / 100;
    for (int y = bar_y; y < bar_y + bar_h; y++) {
        for (int x = bar_x; x < bar_x + progress_w; x++) {
            framebuffer[y * (fb_width) + x] = COLOR_ACCENT;
        }
    }
    
    /* Status text */
    void *font = font_get_system(16);
    if (font && driver) {
        font_draw_centered(font, bar_y + 15, driver, COLOR_DIM);
    }
}

static void boot_log(const char *msg) {
    serial_puts("[BOOT] ");
    serial_puts(msg);
    serial_puts("\n");
}

/* ============ Driver Table ============ */

/* Forward declarations */
static int init_stub(void) { return 0; }

static driver_entry_t boot_drivers[] = {
    /* Stage 1: Early */
    /* Core bootstrap stages */
    {"BSP Setup", bsp_init, NULL, BOOT_STAGE_EARLY, 0, 0, 0},
    {"Platform Detection", platform_detect, NULL, BOOT_STAGE_EARLY, 0, 0, 0},
    {"Platform Init", platform_init, NULL, BOOT_STAGE_EARLY, 0, 0, 0 },
    {"Serial Console", serial_init, NULL, BOOT_STAGE_EARLY, 0, 0, 0},
    {"Memory Manager", pmm_init, NULL, BOOT_STAGE_EARLY, 0, 0, 0},
    {"Font System", font_init, NULL, BOOT_STAGE_EARLY, 0, 0, 0},
    
    /* Stage 2: Core */
    {"Interrupt Descriptor Table", init_stub, NULL, BOOT_STAGE_CORE, 0, 0, 0},
    {"Timer (PIT)", init_stub, NULL, BOOT_STAGE_CORE, 0, 0, 0},
    
    /* Stage 3: Hardware */
    {"USB (xHCI)", usb_init, NULL, BOOT_STAGE_HARDWARE, 0, 1, 0},
    {"Storage (AHCI)", ahci_init, NULL, BOOT_STAGE_HARDWARE, 0, 0, 0},
    
    /* Stage 4: Network */
    {"Network Core", network_init, NULL, BOOT_STAGE_NETWORK, 0, 0, 0},
    {"Ethernet (e1000)", e1000_init, NULL, BOOT_STAGE_NETWORK, 0, 1, 0},
    {"WiFi", wifi_init, NULL, BOOT_STAGE_NETWORK, 0, 1, 0},
    {"Bluetooth", bluetooth_init, NULL, BOOT_STAGE_NETWORK, 0, 1, 0},
    {"TCP/IP Stack", tcpip_init, NULL, BOOT_STAGE_NETWORK, 0, 0, 0},
    
    /* Stage 5: Filesystem */
    {"Virtual Filesystem", vfs_init, NULL, BOOT_STAGE_FILESYSTEM, 0, 0, 0},
    {"Filesystem Hierarchy", fs_init, NULL, BOOT_STAGE_FILESYSTEM, 0, 0, 0},
    
    /* Stage 6: Late */
    {"GPU", gpu_init, NULL, BOOT_STAGE_LATE, 0, 0, 0},
    {"Audio (NXAudio)", nxaudio_init, NULL, BOOT_STAGE_LATE, 0, 1, 0},
    {"Power Management", power_init, NULL, BOOT_STAGE_LATE, 0, 0, 0},
    {"Print Subsystem", print_init, NULL, BOOT_STAGE_LATE, 0, 1, 0},
    {"Firewall", firewall_init, NULL, BOOT_STAGE_LATE, 0, 0, 0},
    
    /* Stage 7: User/Desktop Services */
    {"System Config", sysconfig_init, NULL, BOOT_STAGE_USER, 0, 0, 0},
    {"App Launcher", app_launcher_init, NULL, BOOT_STAGE_USER, 0, 0, 0},
    {"Session Manager", session_manager_init, NULL, BOOT_STAGE_USER, 0, 0, 0},
    
    {NULL, NULL, NULL, 0, 0, 0, 0}  /* Terminator */
};

/* ============ Boot Sequence ============ */

static void boot_run_stage(boot_stage_t stage, const char *stage_name) {
    boot_log(stage_name);
    boot_status.current_stage = stage;
    
    int count = 0;
    int stage_drivers = 0;
    
    /* Count drivers in this stage */
    for (int i = 0; boot_drivers[i].name; i++) {
        if (boot_drivers[i].stage == stage) stage_drivers++;
    }
    
    /* Initialize drivers in this stage */
    for (int i = 0; boot_drivers[i].name; i++) {
        if (boot_drivers[i].stage != stage) continue;
        
        const char *name = boot_drivers[i].name;
        
        serial_puts("  -> ");
        serial_puts(name);
        serial_puts("... ");
        
        /* Update progress */
        int percent = (boot_status.initialized_drivers * 100) / boot_status.total_drivers;
        boot_draw_progress(stage_name, name, percent);
        
        if (progress_callback) {
            progress_callback(stage, name, percent);
        }
        
        /* Call init function */
        int result = 0;
        if (boot_drivers[i].init) {
            result = boot_drivers[i].init();
        }
        
        if (result == 0) {
            serial_puts("OK\n");
            boot_drivers[i].initialized = 1;
            boot_status.initialized_drivers++;
        } else {
            if (boot_drivers[i].optional) {
                serial_puts("SKIPPED\n");
            } else {
                serial_puts("FAILED\n");
                boot_status.failed_drivers++;
                boot_status.failed_driver = name;
            }
        }
        
        count++;
    }
}

int boot_init(uint32_t flags) {
    boot_status.flags = flags;
    boot_status.current_stage = BOOT_STAGE_ENTRY;
    boot_status.initialized_drivers = 0;
    boot_status.failed_drivers = 0;
    
    /* Count total drivers */
    boot_status.total_drivers = 0;
    for (int i = 0; boot_drivers[i].name; i++) {
        boot_status.total_drivers++;
    }
    
    serial_puts("\n");
    serial_puts("  ╔═══════════════════════════════════════╗\n");
    serial_puts("  ║                                       ║\n");
    serial_puts("  ║          NeolyxOS Boot                ║\n");
    serial_puts("  ║                                       ║\n");
    serial_puts("  ╚═══════════════════════════════════════╝\n");
    serial_puts("\n");
    
    if (flags & BOOT_FLAG_VERBOSE) {
        boot_log("Verbose mode enabled");
    }
    
    if (flags & BOOT_FLAG_SAFE_MODE) {
        boot_log("Safe mode - minimal drivers");
    }
    
    /* Run each boot stage */
    boot_run_stage(BOOT_STAGE_EARLY, "Stage 1: Early Initialization");
    boot_run_stage(BOOT_STAGE_CORE, "Stage 2: Core Subsystems");
    boot_run_stage(BOOT_STAGE_HARDWARE, "Stage 3: Hardware Drivers");
    boot_run_stage(BOOT_STAGE_NETWORK, "Stage 4: Network Stack");
    boot_run_stage(BOOT_STAGE_FILESYSTEM, "Stage 5: Filesystems");
    boot_run_stage(BOOT_STAGE_LATE, "Stage 6: Late Drivers");
    boot_run_stage(BOOT_STAGE_USER, "Stage 7: User Space");
    
    /* Boot complete */
    boot_complete();
    
    return 0;
}

boot_status_t *boot_get_status(void) {
    return &boot_status;
}

boot_stage_t boot_get_stage(void) {
    return boot_status.current_stage;
}

void boot_set_progress_callback(boot_progress_fn callback) {
    progress_callback = callback;
}

int boot_register_driver(driver_entry_t *driver) {
    /* TODO: Dynamic driver registration */
    (void)driver;
    return 0;
}

/* ============ Boot Complete ============ */

void boot_complete(void) {
    boot_status.current_stage = BOOT_STAGE_COMPLETE;
    
    serial_puts("\n");
    serial_puts("  ╔═══════════════════════════════════════╗\n");
    serial_puts("  ║                                       ║\n");
    serial_puts("  ║         Boot Complete!                ║\n");
    serial_puts("  ║                                       ║\n");
    serial_puts("  ║   Drivers: ");
    serial_putc('0' + boot_status.initialized_drivers / 10);
    serial_putc('0' + boot_status.initialized_drivers % 10);
    serial_puts(" OK, ");
    serial_putc('0' + boot_status.failed_drivers);
    serial_puts(" failed                ║\n");
    serial_puts("  ║                                       ║\n");
    serial_puts("  ╚═══════════════════════════════════════╝\n");
    serial_puts("\n");
    
    /* Update display */
    boot_draw_progress("Complete", "System Ready", 100);
    
    /* Play boot sound */
    nxaudio_play_boot_sound();
    
    /* Show power status */
    power_print_status();
    
    /* Check system profile */
    if (sysconfig_get_profile() == SYSPROFILE_DESKTOP) {
        boot_log("Starting Desktop Environment...");
        /* TODO: Start window manager */
    } else {
        boot_log("Starting Server Mode...");
        /* TODO: Start SSH server, etc. */
    }
}

int boot_is_complete(void) {
    return boot_status.current_stage == BOOT_STAGE_COMPLETE;
}

/* ============ Shutdown ============ */

int boot_shutdown(void) {
    boot_log("Shutting down...");
    
    /* Shutdown in reverse order */
    for (int stage = BOOT_STAGE_USER; stage >= BOOT_STAGE_EARLY; stage--) {
        for (int i = 0; boot_drivers[i].name; i++) {
            if (boot_drivers[i].stage == stage && boot_drivers[i].initialized) {
                if (boot_drivers[i].shutdown) {
                    serial_puts("  Stopping ");
                    serial_puts(boot_drivers[i].name);
                    serial_puts("...\n");
                    boot_drivers[i].shutdown();
                }
            }
        }
    }
    
    return 0;
}
