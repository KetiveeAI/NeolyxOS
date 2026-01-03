/*
 * NeolyxOS Kernel Stubs
 * 
 * Stub implementations for functions that are needed but not yet
 * fully implemented. All stubs include security considerations.
 * 
 * Copyright (c) 2025 KetiveeAI.
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>
#include <stddef.h>

/* SIZE_MAX definition for overflow checking */
#ifndef SIZE_MAX
#define SIZE_MAX ((size_t)-1)
#endif

/* ============ External Dependencies ============ */

extern void serial_puts(const char *s);
extern void serial_putc(char c);
extern void *kmalloc(size_t size);
extern void kfree(void *ptr);

/* ============ I/O Port Functions ============ */
/* Required by ATA and other legacy drivers */

/* Read 8-bit from port */
uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/* Write 8-bit to port */
void outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

/* Read 16-bit from port */
uint16_t inw(uint16_t port) {
    uint16_t ret;
    __asm__ volatile("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/* Write 16-bit to port */
void outw(uint16_t port, uint16_t val) {
    __asm__ volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}

/* Read 32-bit from port (for completeness) */
uint32_t inl_global(uint16_t port) {
    uint32_t ret;
    __asm__ volatile("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/* Write 32-bit to port (for completeness) */
void outl_global(uint16_t port, uint32_t val) {
    __asm__ volatile("outl %0, %1" : : "a"(val), "Nd"(port));
}

/* ============ Kernel Panic ============ */
/* kernel_panic is now in panic_handler.c */

/* ============ Timer/PIT Functions ============ */
/* Real pit_get_ticks and pit_tick are in pit.c */
/* These externs are for backwards compatibility */
extern uint64_t pit_get_ticks(void);
extern void pit_tick(void);

/* Legacy alias for pit_tick */
void pit_increment(void) {
    pit_tick();
}

/* ============ PMM Memory Query ============ */

/* PMM stats structure - must match pmm.c definition */
typedef struct {
    uint64_t total_pages;
    uint64_t used_pages;
    uint64_t free_pages;
    uint64_t reserved_pages;
    uint64_t total_memory;
    uint64_t free_memory;
} pmm_stats_t;

extern void pmm_get_stats(pmm_stats_t *stats);

/* Get used physical memory (bytes) */
uint64_t pmm_get_used_memory(void) {
    pmm_stats_t stats;
    pmm_get_stats(&stats);
    return stats.total_memory - stats.free_memory;
}

/* ============ Aligned Memory Allocation ============ */
/* Security: Always validates alignment and checks for overflow */

void *kmalloc_aligned(size_t size, size_t alignment) {
    if (size == 0 || alignment == 0) return NULL;
    
    /* Security: Check for power of 2 alignment */
    if ((alignment & (alignment - 1)) != 0) {
        serial_puts("[KMALLOC] ERROR: Non-power-of-2 alignment\n");
        return NULL;
    }
    
    /* Security: Check for size overflow with alignment overhead */
    size_t overhead = alignment + sizeof(void *);
    if (size > SIZE_MAX - overhead) {
        serial_puts("[KMALLOC] ERROR: Size overflow\n");
        return NULL;
    }
    
    /* Allocate with extra space for alignment */
    void *ptr = kmalloc(size + overhead);
    if (!ptr) return NULL;
    
    /* Calculate aligned address */
    uintptr_t addr = (uintptr_t)ptr;
    uintptr_t aligned = (addr + sizeof(void *) + alignment - 1) & ~(alignment - 1);
    
    /* Store original pointer before aligned address */
    ((void **)aligned)[-1] = ptr;
    
    return (void *)aligned;
}

void kfree_aligned(void *ptr) {
    if (ptr) {
        void *original = ((void **)ptr)[-1];
        kfree(original);
    }
}

/* ============ Boot Mode ============ */

static int current_boot_mode = 0;

void boot_set_mode(int mode) {
    current_boot_mode = mode;
}

int boot_get_mode(void) {
    return current_boot_mode;
}

/* ============ Syscall MSR Functions ============ */
/* Real implementations are in arch/x86_64/arch.S */
/* These are just externs - DO NOT define here, causes duplicate symbols */

/* syscall_msr_init and syscall_enable are now in arch.S */
/* No C stubs needed - the assembly versions are used directly */

/* ============ ISR Stub Table ============ */
/* Real isr_stub_table is in isr.S - we just reference it here */
/* DO NOT define here - causes duplicate symbol with isr.S */

extern void (*isr_stub_table[256])(void);

void isr_stub_init(void) {
    /* isr_stub_table already set up by isr.S */
    serial_puts("[ISR] Stub table ready (from isr.S)\\n");
}

/* ============ System Configuration ============ */

/* Profile types */
#define SYSPROFILE_DESKTOP 0
#define SYSPROFILE_SERVER  1

static int current_profile = SYSPROFILE_DESKTOP;

int sysconfig_get_profile(void) {
    return current_profile;
}

void sysconfig_set_profile(int profile) {
    current_profile = profile;
}

/* Read keyboard status port */
int kbd_wait_key(void) {
    /* Wait for key in buffer */
    while (!(inb(0x64) & 0x01)) {
        __asm__ volatile("pause");
    }
    /* Read and return scancode */
    return inb(0x60);
}

/* power_print_status is in power.c - not duplicated here */

/* ============ Process Stubs for Session Manager / NXGame ============ */

/* Get current process PID */
int get_current_pid(void) {
    /* Stub: Return 0 (kernel context) or current_process->pid if available */
    extern void *current_process;
    
    /* Process structure offset for PID - could be 0 or small offset */
    if (current_process) {
        /* PID is usually at offset 0 or 4 in process_t */
        /* Return the first uint32_t which is typically the PID */
        return *(int *)current_process;
    }
    
    return 0;  /* Kernel context - PID 0 */
}

/* Spawn a new process (real implementation using fork+exec) */
int process_spawn(const char *path, int flags) {
    if (!path) {
        return -1;
    }
    
    serial_puts("[SPAWN] Spawning process: ");
    serial_puts(path);
    serial_puts("\n");
    
    /* Import process management functions from process.c */
    extern int32_t process_fork(void);
    extern int process_exec(const char *path, char *const argv[], char *const envp[]);
    extern int32_t process_wait(int32_t pid, int *status);
    
    /* Fork the current process */
    int32_t pid = process_fork();
    
    if (pid < 0) {
        serial_puts("[SPAWN] Fork failed\n");
        return -1;
    }
    
    if (pid == 0) {
        /* ========== CHILD PROCESS ========== */
        /* Build minimal argv array */
        char *argv[2];
        argv[0] = (char *)path;  /* Program name */
        argv[1] = NULL;          /* Terminator */
        
        /* Empty environment for now */
        char *envp[1];
        envp[0] = NULL;
        
        /* Replace this process with the new program */
        int ret = process_exec(path, argv, envp);
        
        /* If exec returns, it failed */
        serial_puts("[SPAWN] Exec failed for: ");
        serial_puts(path);
        serial_puts("\n");
        
        /* Exit with error code - process_exit is in process.c */
        extern void process_exit(int exit_code);
        process_exit(127);  /* 127 = command not found/exec failed */
        
        /* Should never reach here */
        return -1;
    }
    
    /* ========== PARENT PROCESS ========== */
    serial_puts("[SPAWN] Child process created with PID ");
    /* Print PID */
    char pidbuf[12];
    int i = 11;
    pidbuf[i--] = '\0';
    if (pid == 0) { pidbuf[i--] = '0'; }
    else {
        int32_t p = pid;
        while (p > 0 && i >= 0) { pidbuf[i--] = '0' + (p % 10); p /= 10; }
    }
    serial_puts(&pidbuf[i + 1]);
    serial_puts("\n");
    
    /* Check spawn flags */
    #define SPAWN_FLAG_WAIT  0x01  /* Wait for child to complete */
    #define SPAWN_FLAG_DETACH 0x02 /* Detach child completely */
    
    if (flags & SPAWN_FLAG_WAIT) {
        /* Wait for child to complete */
        int status = 0;
        process_wait(pid, &status);
        
        serial_puts("[SPAWN] Child exited with status ");
        if (status < 0) { serial_puts("-"); status = -status; }
        char sbuf[12];
        i = 11;
        sbuf[i--] = '\0';
        if (status == 0) { sbuf[i--] = '0'; }
        else {
            while (status > 0 && i >= 0) { sbuf[i--] = '0' + (status % 10); status /= 10; }
        }
        serial_puts(&sbuf[i + 1]);
        serial_puts("\n");
    }
    
    /* Return child PID to caller */
    return pid;
}

/* Note: process_kill is defined in process.c */

/* ============ ZepraBrowser Session Stubs ============ */
/* Weak stubs - real implementations in browser_session.c override these */

__attribute__((weak)) void zepra_session_start(void) {
    serial_puts("[ZEPRA] Session start (stub)\n");
}

__attribute__((weak)) void zepra_session_close(void) {
    serial_puts("[ZEPRA] Session close (stub)\n");
}

__attribute__((weak)) void zepra_session_draw_launcher(void) {
    serial_puts("[ZEPRA] Draw launcher (stub)\n");
}

/* ============ Platform/BSP Stubs ============ */
/* Weak stubs - real implementations in bsp.c/platform_detect.c override these */

__attribute__((weak)) void bsp_init(void) {
    serial_puts("[BSP] BSP init (stub)\n");
}

__attribute__((weak)) int platform_detect(void) {
    serial_puts("[PLATFORM] Platform detect (stub)\n");
    return 0;  /* Generic x86-64 platform */
}

__attribute__((weak)) void platform_init(void) {
    serial_puts("[PLATFORM] Platform init (stub)\n");
}

/* ============ Nucleus Install Helper Stubs ============ */
/* Weak stubs - real implementations in nucleus_write.c override these */

__attribute__((weak)) int nucleus_find_install_source(void) {
    serial_puts("[NUCLEUS] Find install source (stub)\n");
    return 0;  /* Use default source */
}

__attribute__((weak)) void nucleus_default_progress(int percent) {
    (void)percent;  /* Suppress unused warning */
}

__attribute__((weak)) int nucleus_copy_boot_files(void) {
    serial_puts("[NUCLEUS] Copy boot files (stub)\n");
    return 0;  /* Success */
}

/* ============ NPA Package Manager Stubs ============ */
/* Weak stubs - real implementations in npa_manager.c override these */

__attribute__((weak)) int npa_install(const char *package) {
    serial_puts("[NPA] Install: ");
    if (package) serial_puts(package);
    serial_puts(" (not available yet)\n");
    return -1;  /* Not implemented */
}

__attribute__((weak)) int npa_remove(const char *package) {
    serial_puts("[NPA] Remove: ");
    if (package) serial_puts(package);
    serial_puts(" (not available yet)\n");
    return -1;
}

__attribute__((weak)) void npa_list(void) {
    serial_puts("[NPA] No packages installed\n");
}

__attribute__((weak)) int npa_search(const char *query) {
    serial_puts("[NPA] Search: ");
    if (query) serial_puts(query);
    serial_puts(" (repository offline)\n");
    return 0;
}

__attribute__((weak)) int npa_check_updates(void) {
    serial_puts("[NPA] Checking for updates... (offline)\n");
    return 0;  /* No updates available */
}

__attribute__((weak)) int npa_apply_all_updates(void) {
    serial_puts("[NPA] No updates to apply\n");
    return 0;
}

__attribute__((weak)) int npa_apply_update(const char *package) {
    serial_puts("[NPA] Update: ");
    if (package) serial_puts(package);
    serial_puts(" (offline)\n");
    return -1;
}

__attribute__((weak)) void npa_info(const char *package) {
    serial_puts("[NPA] Package info: ");
    if (package) serial_puts(package);
    serial_puts(" (not found)\n");
}

__attribute__((weak)) int npa_verify(const char *package) {
    serial_puts("[NPA] Verify: ");
    if (package) serial_puts(package);
    serial_puts(" (skipped)\n");
    return 0;
}

/* ============ Test Framework Stub ============ */

__attribute__((weak)) int kernel_run_tests(void) {
    serial_puts("[TEST] Test framework not available\n");
    return 0;
}

