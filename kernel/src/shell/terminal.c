/*
 * NeolyxOS Terminal Implementation
 * 
 * Command Parser + Executor
 * Human-first, security-aware, predictable
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include "shell/terminal.h"
#include "fs/vfs.h"
#include "core/npa_manager.h"

/* ============ String Utilities (no libc) ============ */

static int str_len(const char *s) {
    int len = 0;
    while (s[len]) len++;
    return len;
}

static void str_copy(char *dst, const char *src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static int str_cmp(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return *a == *b;
}

static int str_starts_with(const char *str, const char *prefix) {
    while (*prefix) {
        if (*str++ != *prefix++) return 0;
    }
    return 1;
}

static void str_cat(char *dst, const char *src, int max) {
    int len = str_len(dst);
    int i = 0;
    while (src[i] && len + i < max - 1) {
        dst[len + i] = src[i];
        i++;
    }
    dst[len + i] = '\0';
}

/* Convert uint64 to string */
static void int_to_str(uint64_t num, char *buf, int max) {
    if (max < 2) { buf[0] = '\0'; return; }
    
    if (num == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return;
    }
    
    char tmp[24];
    int i = 0;
    while (num > 0 && i < 23) {
        tmp[i++] = '0' + (num % 10);
        num /= 10;
    }
    
    /* Reverse into buf */
    int j = 0;
    while (i > 0 && j < max - 1) {
        buf[j++] = tmp[--i];
    }
    buf[j] = '\0';
}

/* ============ Command Handlers ============ */

/* help - List all commands */
term_status_t cmd_help(terminal_state_t *term, int argc, char **argv) {
    terminal_println(term, "NeolyxOS Commands:");
    terminal_println(term, "");
    terminal_println(term, "  help          Show this help");
    terminal_println(term, "  clear         Clear screen");
    terminal_println(term, "  version       Show OS version");
    terminal_println(term, "  exit          Exit terminal");
    terminal_println(term, "");
    terminal_println(term, "  info system   System overview");
    terminal_println(term, "  info memory   Memory usage");
    terminal_println(term, "  info disk     Disk usage");
    terminal_println(term, "  info cpu      CPU information");
    terminal_println(term, "  uptime        System uptime");
    terminal_println(term, "");
    terminal_println(term, "  pwd           Current directory");
    terminal_println(term, "  list          List files");
    terminal_println(term, "  cd <path>     Change directory");
    terminal_println(term, "  show <file>   Display file");
    terminal_println(term, "");
    terminal_println(term, "  exec <path>   Execute user program");
    terminal_println(term, "  run <path>    Run program (alias)");
    terminal_println(term, "  tasks         Show running tasks");
    terminal_println(term, "");
    terminal_println(term, "  sys status    Privilege level");
    terminal_println(term, "  sys reboot    Restart system");
    terminal_println(term, "  sys shutdown  Power off");
    terminal_println(term, "");
    terminal_println(term, "Use 'sys' prefix for system operations.");
    return TERM_OK;
}

/* clear - Clear terminal */
term_status_t cmd_clear(terminal_state_t *term, int argc, char **argv) {
    terminal_clear_output(term);
    
    /* Actually clear the console screen */
    extern void console_clear(void);
    console_clear();
    
    /* Output will include banner on next render */
    return TERM_OK;
}

/* exit - Exit terminal */
term_status_t cmd_exit(terminal_state_t *term, int argc, char **argv) {
    term->running = 0;
    return TERM_EXIT;
}

/* version - Show OS version */
term_status_t cmd_version(terminal_state_t *term, int argc, char **argv) {
    terminal_println(term, "NeolyxOS v1.0.0 (Alpha)");
    terminal_println(term, "Build: 2025-12-15");
    terminal_println(term, "Copyright (c) 2025 KetiveeAI");
    return TERM_OK;
}

/* info system - System overview */
term_status_t cmd_info_system(terminal_state_t *term, int argc, char **argv) {
    terminal_println(term, "System Information:");
    terminal_println(term, "  OS:       NeolyxOS v1.0.0");
    terminal_println(term, "  Arch:     x86_64");
    terminal_println(term, "  Boot:     UEFI");
    terminal_println(term, "  Kernel:   NeolyxKernel");
    terminal_println(term, "  Shell:    NeolyxTerminal");
    return TERM_OK;
}

/* info memory - Memory usage (GET DATA from PMM) */
term_status_t cmd_info_memory(terminal_state_t *term, int argc, char **argv) {
    /* Get real memory stats from Physical Memory Manager */
    typedef struct {
        uint64_t total_pages;
        uint64_t used_pages;
        uint64_t free_pages;
        uint64_t reserved_pages;
        uint64_t total_memory;
        uint64_t free_memory;
    } pmm_stats_t;
    
    extern void pmm_get_stats(pmm_stats_t *stats);
    extern int pmm_is_initialized(void);
    
    pmm_stats_t stats;
    
    if (!pmm_is_initialized()) {
        terminal_println(term, "Memory Information:");
        terminal_println(term, "  PMM not initialized");
        return TERM_OK;
    }
    
    pmm_get_stats(&stats);
    
    /* Convert to MB */
    uint64_t total_mb = stats.total_memory / (1024 * 1024);
    uint64_t free_mb = stats.free_memory / (1024 * 1024);
    uint64_t used_mb = (stats.total_memory - stats.free_memory) / (1024 * 1024);
    
    /* Build strings with numbers */
    char buf[64];
    
    terminal_println(term, "Memory Information:");
    
    terminal_print(term, "  Total:    ");
    int_to_str(total_mb, buf, sizeof(buf));
    terminal_print(term, buf);
    terminal_println(term, " MB");
    
    terminal_print(term, "  Used:     ");
    int_to_str(used_mb, buf, sizeof(buf));
    terminal_print(term, buf);
    terminal_println(term, " MB");
    
    terminal_print(term, "  Free:     ");
    int_to_str(free_mb, buf, sizeof(buf));
    terminal_print(term, buf);
    terminal_println(term, " MB");
    
    terminal_print(term, "  Pages:    ");
    int_to_str(stats.total_pages, buf, sizeof(buf));
    terminal_print(term, buf);
    terminal_print(term, " (");
    int_to_str(stats.free_pages, buf, sizeof(buf));
    terminal_print(term, buf);
    terminal_println(term, " free)");
    
    return TERM_OK;
}

/* info disk - Disk usage (GET DATA from AHCI) */
term_status_t cmd_info_disk(terminal_state_t *term, int argc, char **argv) {
    /* AHCI drive info structure - must match kernel's ahci_drive_t exactly */
    typedef struct {
        int port;               /* AHCI port number */
        int type;               /* SATA / ATAPI */
        uint64_t sectors;       /* Total sectors */
        char model[41];         /* Model string */
        char serial[21];        /* Serial number */
        int present;
    } ahci_drive_t;
    
    extern int ahci_probe_drives(ahci_drive_t *drives, int max);
    
    ahci_drive_t drives[4];
    int count = ahci_probe_drives(drives, 4);
    
    terminal_println(term, "Disk Information:");
    
    if (count == 0) {
        terminal_println(term, "  No drives detected");
        return TERM_OK;
    }
    
    char buf[64];
    
    for (int i = 0; i < count; i++) {
        if (!drives[i].present) continue;
        
        /* Calculate size in MB (512 bytes per sector) */
        uint64_t size_mb = (drives[i].sectors * 512) / (1024 * 1024);
        
        terminal_print(term, "  Drive ");
        int_to_str(i, buf, sizeof(buf));
        terminal_print(term, buf);
        terminal_println(term, ":");
        
        terminal_print(term, "    Model:  ");
        terminal_println(term, drives[i].model);
        
        terminal_print(term, "    Size:   ");
        int_to_str(size_mb, buf, sizeof(buf));
        terminal_print(term, buf);
        terminal_println(term, " MB");
        
        terminal_print(term, "    Port:   ");
        int_to_str(drives[i].port, buf, sizeof(buf));
        terminal_println(term, buf);
    }
    
    return TERM_OK;
}

/* info cpu - CPU info (REAL DATA from CPUID) */
term_status_t cmd_info_cpu(terminal_state_t *term, int argc, char **argv) {
    terminal_println(term, "CPU Information:");
    
    /* Get CPU vendor string using CPUID */
    uint32_t eax, ebx, ecx, edx;
    char vendor[13];
    
    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0));
    
    /* Vendor string is in ebx:edx:ecx order */
    *(uint32_t*)&vendor[0] = ebx;
    *(uint32_t*)&vendor[4] = edx;
    *(uint32_t*)&vendor[8] = ecx;
    vendor[12] = '\0';
    
    terminal_print(term, "  Vendor:   ");
    terminal_println(term, vendor);
    
    /* Get CPU brand string (if supported) */
    __asm__ volatile("cpuid" : "=a"(eax) : "a"(0x80000000) : "ebx", "ecx", "edx");
    
    if (eax >= 0x80000004) {
        char brand[49];
        uint32_t *brand32 = (uint32_t*)brand;
        
        __asm__ volatile("cpuid" : "=a"(brand32[0]), "=b"(brand32[1]), "=c"(brand32[2]), "=d"(brand32[3]) : "a"(0x80000002));
        __asm__ volatile("cpuid" : "=a"(brand32[4]), "=b"(brand32[5]), "=c"(brand32[6]), "=d"(brand32[7]) : "a"(0x80000003));
        __asm__ volatile("cpuid" : "=a"(brand32[8]), "=b"(brand32[9]), "=c"(brand32[10]), "=d"(brand32[11]) : "a"(0x80000004));
        brand[48] = '\0';
        
        terminal_print(term, "  Model:    ");
        terminal_println(term, brand);
    } else {
        terminal_println(term, "  Model:    x86_64 Compatible");
    }
    
    /* Get core count (basic - would need APIC enum for real count) */
    terminal_println(term, "  Arch:     x86_64");
    
    return TERM_OK;
}

/* uptime - System uptime (REAL DATA from timer) */
term_status_t cmd_uptime(terminal_state_t *term, int argc, char **argv) {
    extern uint32_t timer_get_ticks(void);
    extern uint32_t timer_get_frequency(void);
    
    uint32_t ticks = timer_get_ticks();
    uint32_t freq = timer_get_frequency();
    
    if (freq == 0) freq = 100; /* Default 100Hz */
    
    /* Calculate time */
    uint32_t total_seconds = ticks / freq;
    uint32_t days = total_seconds / 86400;
    uint32_t hours = (total_seconds % 86400) / 3600;
    uint32_t minutes = (total_seconds % 3600) / 60;
    uint32_t seconds = total_seconds % 60;
    
    char buf[16];
    
    terminal_print(term, "Uptime: ");
    int_to_str(days, buf, sizeof(buf));
    terminal_print(term, buf);
    terminal_print(term, " days, ");
    int_to_str(hours, buf, sizeof(buf));
    terminal_print(term, buf);
    terminal_print(term, " hours, ");
    int_to_str(minutes, buf, sizeof(buf));
    terminal_print(term, buf);
    terminal_print(term, " minutes, ");
    int_to_str(seconds, buf, sizeof(buf));
    terminal_print(term, buf);
    terminal_println(term, " seconds");
    
    return TERM_OK;
}

/* sys status - Show privilege level */
term_status_t cmd_sys_status(terminal_state_t *term, int argc, char **argv) {
    if (term->privilege == PRIV_SYSTEM) {
        terminal_println(term, "Privilege Level: SYSTEM");
        terminal_println(term, "All commands available.");
    } else {
        terminal_println(term, "Privilege Level: USER");
        terminal_println(term, "Use 'sys' prefix for system commands.");
    }
    return TERM_OK;
}

/* sys reboot - Reboot system */
term_status_t cmd_sys_reboot(terminal_state_t *term, int argc, char **argv) {
    if (term->privilege != PRIV_SYSTEM) {
        terminal_println(term, "error: permission denied");
        terminal_println(term, "use 'sys reboot' for system operations");
        return TERM_ERR_PERMISSION;
    }
    terminal_println(term, "Rebooting system...");
    
    /* x86 reboot via keyboard controller */
    /* Wait for keyboard controller ready */
    extern void serial_puts(const char *s);
    serial_puts("[REBOOT] Resetting system via keyboard controller...\n");
    
    /* Inline assembly for port I/O */
    __asm__ volatile(
        "cli\n"                    /* Disable interrupts */
        "1:\n"
        "inb $0x64, %%al\n"        /* Read keyboard status */
        "testb $0x02, %%al\n"      /* Wait for input buffer empty */
        "jnz 1b\n"
        "movb $0xFE, %%al\n"       /* 0xFE = reset command */
        "outb %%al, $0x64\n"       /* Send to keyboard controller */
        "hlt\n"                    /* Halt until reset occurs */
        ::: "al"
    );
    
    /* Should not reach here */
    return TERM_OK;
}

/* sys shutdown - Power off */
term_status_t cmd_sys_shutdown(terminal_state_t *term, int argc, char **argv) {
    if (term->privilege != PRIV_SYSTEM) {
        terminal_println(term, "error: permission denied");
        terminal_println(term, "use 'sys shutdown' for system operations");
        return TERM_ERR_PERMISSION;
    }
    terminal_println(term, "Shutting down...");
    
    /* ACPI shutdown - works on QEMU and most systems */
    extern void serial_puts(const char *s);
    serial_puts("[SHUTDOWN] Initiating ACPI power off...\n");
    
    /* Inline assembly for port I/O */
    /* QEMU uses port 0xB004 (isa-debug-exit) or 0x604 for ACPI shutdown */
    __asm__ volatile(
        "cli\n"                      /* Disable interrupts */
        "movw $0x2000, %%ax\n"       /* S5 state = power off */
        "movw $0xB004, %%dx\n"       /* QEMU isa-debug-exit port */
        "outw %%ax, %%dx\n"          /* Send shutdown command */
        "movw $0x0604, %%dx\n"       /* Alternative ACPI port */
        "movw $0x2000, %%ax\n"
        "outw %%ax, %%dx\n"
        "hlt\n"                      /* Halt if shutdown fails */
        ::: "ax", "dx"
    );
    
    /* Should not reach here */
    return TERM_OK;
}

/* pwd - Print working directory */
term_status_t cmd_pwd(terminal_state_t *term, int argc, char **argv) {
    terminal_println(term, term->cwd);
    return TERM_OK;
}

/* cd - Change directory */
term_status_t cmd_cd(terminal_state_t *term, int argc, char **argv) {
    /* Build home path: /Users/{username} */
    char home_path[128];
    home_path[0] = '\0';
    str_cat(home_path, "/Users/", sizeof(home_path));
    str_cat(home_path, term->username, sizeof(home_path));
    
    if (argc < 2) {
        str_copy(term->cwd, home_path, TERM_CWD_MAX);
    } else if (str_cmp(argv[1], "~")) {
        str_copy(term->cwd, home_path, TERM_CWD_MAX);
    } else if (str_cmp(argv[1], "..")) {
        /* Go to parent */
        int len = str_len(term->cwd);
        while (len > 1 && term->cwd[len - 1] != '/') len--;
        if (len > 1) len--;
        term->cwd[len] = '\0';
        if (len == 0) str_copy(term->cwd, "/", TERM_CWD_MAX);
    } else if (argv[1][0] == '/') {
        /* Absolute path */
        str_copy(term->cwd, argv[1], TERM_CWD_MAX);
    } else {
        /* Relative path */
        if (!str_cmp(term->cwd, "/")) {
            str_cat(term->cwd, "/", TERM_CWD_MAX);
        }
        str_cat(term->cwd, argv[1], TERM_CWD_MAX);
    }
    return TERM_OK;
}

/* list - List directory */
term_status_t cmd_list(terminal_state_t *term, int argc, char **argv) {
    int show_all = 0;
    const char *target = term->cwd;
    
    /* Parse options */
    for (int i = 1; i < argc; i++) {
        if (str_cmp(argv[i], "all")) {
            show_all = 1;
        }
    }
    
    /* Use VFS to list directory */
    vfs_dirent_t entries[32];
    uint32_t count = 0;
    
    int result = vfs_readdir(target, entries, 32, &count);
    if (result != 0) {
        terminal_print(term, "error: cannot read directory: ");
        terminal_println(term, target);
        return TERM_ERR_NOT_FOUND;
    }
    
    if (count == 0) {
        terminal_println(term, "  (empty)");
        return TERM_OK;
    }
    
    /* Print directory contents */
    for (uint32_t i = 0; i < count; i++) {
        const char *name = entries[i].name;
        
        /* Skip hidden files unless 'all' specified */
        if (name[0] == '.' && !show_all) {
            continue;
        }
        
        terminal_print(term, "  ");
        terminal_print(term, name);
        
        /* Add trailing / for directories */
        if (entries[i].type == VFS_DIRECTORY) {
            terminal_print(term, "/");
        }
        
        terminal_println(term, "");
    }
    
    return TERM_OK;
}

/* show - Display file */
term_status_t cmd_show(terminal_state_t *term, int argc, char **argv) {
    if (argc < 2) {
        terminal_println(term, "error: missing argument");
        terminal_println(term, "usage: show <filename>");
        return TERM_ERR_INVALID_ARGS;
    }
    
    /* Build full path */
    char path[256];
    if (argv[1][0] == '/') {
        str_copy(path, argv[1], sizeof(path));
    } else {
        str_copy(path, term->cwd, sizeof(path));
        if (!str_cmp(term->cwd, "/")) {
            str_cat(path, "/", sizeof(path));
        }
        str_cat(path, argv[1], sizeof(path));
    }
    
    /* Open file */
    vfs_file_t *file = vfs_open(path, VFS_O_RDONLY);
    if (!file) {
        terminal_print(term, "error: file not found: ");
        terminal_println(term, path);
        return TERM_ERR_NOT_FOUND;
    }
    
    /* Read and display contents */
    char buf[512];
    int64_t bytes;
    while ((bytes = vfs_read(file, buf, sizeof(buf) - 1)) > 0) {
        buf[bytes] = '\0';
        terminal_print(term, buf);
    }
    terminal_println(term, "");
    
    vfs_close(file);
    return TERM_OK;
}

/* make file/dir - Create file or directory */
term_status_t cmd_make(terminal_state_t *term, int argc, char **argv) {
    if (argc < 3) {
        terminal_println(term, "error: missing arguments");
        terminal_println(term, "usage: make file <name>");
        terminal_println(term, "       make dir <name>");
        return TERM_ERR_INVALID_ARGS;
    }
    
    const char *type = argv[1];
    const char *name = argv[2];
    
    /* Build full path */
    char path[256];
    if (name[0] == '/') {
        str_copy(path, name, sizeof(path));
    } else {
        str_copy(path, term->cwd, sizeof(path));
        if (!str_cmp(term->cwd, "/")) {
            str_cat(path, "/", sizeof(path));
        }
        str_cat(path, name, sizeof(path));
    }
    
    /* Determine type */
    vfs_type_t vfs_type;
    if (str_cmp(type, "file")) {
        vfs_type = VFS_FILE;
    } else if (str_cmp(type, "dir")) {
        vfs_type = VFS_DIRECTORY;
    } else {
        terminal_print(term, "error: unknown type '");
        terminal_print(term, type);
        terminal_println(term, "'");
        terminal_println(term, "use 'make file' or 'make dir'");
        return TERM_ERR_INVALID_ARGS;
    }
    
    /* Create */
    int result = vfs_create(path, vfs_type, 0755);
    if (result != 0) {
        terminal_print(term, "error: failed to create ");
        terminal_println(term, path);
        return TERM_ERR_NOT_FOUND;
    }
    
    terminal_print(term, "created: ");
    terminal_println(term, path);
    return TERM_OK;
}

/* remove - Delete file or directory */
term_status_t cmd_remove(terminal_state_t *term, int argc, char **argv) {
    if (argc < 2) {
        terminal_println(term, "error: missing argument");
        terminal_println(term, "usage: remove <path>");
        return TERM_ERR_INVALID_ARGS;
    }
    
    /* Build full path */
    char path[256];
    if (argv[1][0] == '/') {
        str_copy(path, argv[1], sizeof(path));
    } else {
        str_copy(path, term->cwd, sizeof(path));
        if (!str_cmp(term->cwd, "/")) {
            str_cat(path, "/", sizeof(path));
        }
        str_cat(path, argv[1], sizeof(path));
    }
    
    /* Check for system paths (require sys prefix) */
    if (str_starts_with(path, "/System") || 
        str_starts_with(path, "/Boot") ||
        str_starts_with(path, "/Applications")) {
        if (term->privilege != PRIV_SYSTEM) {
            terminal_println(term, "error: permission denied");
            terminal_println(term, "use 'sys remove' for system files");
            return TERM_ERR_PERMISSION;
        }
    }
    
    /* Delete */
    int result = vfs_unlink(path);
    if (result != 0) {
        terminal_print(term, "error: failed to remove ");
        terminal_println(term, path);
        return TERM_ERR_NOT_FOUND;
    }
    
    terminal_print(term, "removed: ");
    terminal_println(term, path);
    return TERM_OK;
}

/* ============ NeolyxOS App Framework ============ */

/* nxapp create - Create new app with structure */
term_status_t cmd_nxapp_create(terminal_state_t *term, int argc, char **argv) {
    if (argc < 3) {
        terminal_println(term, "NeolyxOS App Framework");
        terminal_println(term, "");
        terminal_println(term, "usage: nxapp create <AppName>");
        terminal_println(term, "");
        terminal_println(term, "Creates app structure at ~/Applications/<AppName>/");
        terminal_println(term, "with Info.plist, main.c, and Resources/");
        return TERM_ERR_INVALID_ARGS;
    }
    
    const char *app_name = argv[2];
    char base_path[256];
    char path[256];
    
    /* Build base path: /Users/{user}/Applications/{AppName} */
    str_copy(base_path, "/Users/", sizeof(base_path));
    str_cat(base_path, term->username, sizeof(base_path));
    str_cat(base_path, "/Applications/", sizeof(base_path));
    str_cat(base_path, app_name, sizeof(base_path));
    
    terminal_print(term, "Creating app: ");
    terminal_println(term, app_name);
    terminal_println(term, "");
    
    /* Create main app directory */    
    if (vfs_create(base_path, VFS_DIRECTORY, 0755) != 0) {
        terminal_println(term, "error: failed to create app directory");
        return TERM_ERR_PERMISSION;
    }
    terminal_print(term, "  [+] ");
    terminal_println(term, base_path);
    
    /* Create Resources/ */
    str_copy(path, base_path, sizeof(path));
    str_cat(path, "/Resources", sizeof(path));
    vfs_create(path, VFS_DIRECTORY, 0755);
    terminal_println(term, "  [+] Resources/");
    
    /* Create Resources/assets/ */
    str_copy(path, base_path, sizeof(path));
    str_cat(path, "/Resources/assets", sizeof(path));
    vfs_create(path, VFS_DIRECTORY, 0755);
    terminal_println(term, "  [+] Resources/assets/");
    
    /* Create src/ */
    str_copy(path, base_path, sizeof(path));
    str_cat(path, "/src", sizeof(path));
    vfs_create(path, VFS_DIRECTORY, 0755);
    terminal_println(term, "  [+] src/");
    
    /* Create lib/ */
    str_copy(path, base_path, sizeof(path));
    str_cat(path, "/lib", sizeof(path));
    vfs_create(path, VFS_DIRECTORY, 0755);
    terminal_println(term, "  [+] lib/");
    
    /* Create Info.plist */
    str_copy(path, base_path, sizeof(path));
    str_cat(path, "/Info.plist", sizeof(path));
    vfs_create(path, VFS_FILE, 0644);
    terminal_println(term, "  [+] Info.plist");
    
    /* Create src/main.c */
    str_copy(path, base_path, sizeof(path));
    str_cat(path, "/src/main.c", sizeof(path));
    vfs_create(path, VFS_FILE, 0644);
    terminal_println(term, "  [+] src/main.c");
    
    /* Create Makefile */
    str_copy(path, base_path, sizeof(path));
    str_cat(path, "/Makefile", sizeof(path));
    vfs_create(path, VFS_FILE, 0644);
    terminal_println(term, "  [+] Makefile");
    
    /* Create README.md */
    str_copy(path, base_path, sizeof(path));
    str_cat(path, "/README.md", sizeof(path));
    vfs_create(path, VFS_FILE, 0644);
    terminal_println(term, "  [+] README.md");
    
    terminal_println(term, "");
    terminal_println(term, "App created successfully!");
    terminal_println(term, "");
    terminal_println(term, "Next steps:");
    terminal_print(term, "  1. Edit ");
    terminal_print(term, base_path);
    terminal_println(term, "/Info.plist");
    terminal_println(term, "  2. Write your code in src/main.c");
    terminal_println(term, "  3. Build: make");
    terminal_println(term, "  4. Run: ./AppName");
    
    return TERM_OK;
}

/* tasks - Show processes */
term_status_t cmd_tasks(terminal_state_t *term, int argc, char **argv) {
    terminal_println(term, "Running Tasks:");
    terminal_println(term, "  PID  NAME           STATUS");
    terminal_println(term, "  1    kernel         running");
    terminal_println(term, "  2    terminal       running");
    return TERM_OK;
}

/* test - Run kernel test suite */
extern void kernel_run_tests(void);  /* From tests/kernel_tests.c */

term_status_t cmd_test(terminal_state_t *term, int argc, char **argv) {
    (void)argc; (void)argv;
    
    terminal_println(term, "Running kernel test suite...");
    terminal_println(term, "(Output appears on serial console)");
    terminal_println(term, "");
    
    /* Run the comprehensive test suite */
    kernel_run_tests();
    
    terminal_println(term, "");
    terminal_println(term, "Test suite complete. Check serial output for results.");
    return TERM_OK;
}

/* net status (ifconfig) - Show network interface status */
/* Network interface structure (must match net/network.h) */
typedef struct cmd_net_interface {
    char name[16];
    int type;
    int state;
    uint8_t mac[6];
    uint8_t ip[4];
    uint8_t netmask[4];
    uint8_t gateway[4];
    uint8_t dns[4];
} cmd_net_interface_t;

/* External network API */
extern cmd_net_interface_t *network_get_interface(const char *name);
extern int network_interface_count(void);
extern cmd_net_interface_t *network_get_interface_by_index(int index);

term_status_t cmd_net_status(terminal_state_t *term, int argc, char **argv) {
    (void)argc; (void)argv;
    
    terminal_println(term, "Network Interfaces:");
    terminal_println(term, "");
    
    int count = network_interface_count();
    if (count == 0) {
        terminal_println(term, "  No network interfaces detected");
        return TERM_OK;
    }
    
    for (int i = 0; i < count; i++) {
        /* Get interface - returns net_interface_t* */
        cmd_net_interface_t *iface = network_get_interface_by_index(i);
        if (!iface) continue;
        
        terminal_print(term, "  ");
        terminal_print(term, iface->name);
        terminal_print(term, ": ");
        terminal_println(term, iface->state > 0 ? "UP" : "DOWN");
        
        /* Print IP */
        terminal_print(term, "    IP:  ");
        char ip_str[16];
        for (int j = 0; j < 4; j++) {
            int_to_str(iface->ip[j], ip_str, 16);
            terminal_print(term, ip_str);
            if (j < 3) terminal_print(term, ".");
        }
        terminal_println(term, "");
        
        /* Print MAC */
        terminal_print(term, "    MAC: ");
        char hex[] = "0123456789ABCDEF";
        for (int j = 0; j < 6; j++) {
            char mb[3] = {hex[iface->mac[j] >> 4], hex[iface->mac[j] & 0xF], 0};
            terminal_print(term, mb);
            if (j < 5) terminal_print(term, ":");
        }
        terminal_println(term, "");
    }
    
    return TERM_OK;
}

/* ping - Send ICMP echo request */
/* ipv4 address structure for ICMP */
typedef struct {
    uint8_t bytes[4];
} ping_ipv4_addr_t;

/* External ICMP API */
extern int icmp_ping(void *iface, ping_ipv4_addr_t dst, uint16_t seq);
extern int network_up(void *iface);

/* Parse IP address string to uint32 */
static uint32_t parse_ip(const char *str) {
    uint32_t result = 0;
    uint8_t octet = 0;
    int shift = 0;
    
    while (*str) {
        if (*str >= '0' && *str <= '9') {
            octet = octet * 10 + (*str - '0');
        } else if (*str == '.') {
            result |= ((uint32_t)octet << (shift * 8));
            octet = 0;
            shift++;
        }
        str++;
    }
    result |= ((uint32_t)octet << (shift * 8));
    
    return result;
}

term_status_t cmd_ping(terminal_state_t *term, int argc, char **argv) {
    if (argc < 2) {
        terminal_println(term, "usage: ping <ip>");
        terminal_println(term, "  example: ping 10.0.2.2");
        return TERM_ERR_INVALID_ARGS;
    }
    
    /* Get default interface */
    cmd_net_interface_t *iface = network_get_interface("eth0");
    if (!iface) {
        terminal_println(term, "ping: no network interface available");
        return TERM_ERR_IO;
    }
    
    /* Ensure interface is up */
    if (iface->state == 0) {
        network_up(iface);
    }
    
    /* Parse destination IP into addr structure */
    uint32_t dst_raw = parse_ip(argv[1]);
    ping_ipv4_addr_t dst;
    dst.bytes[0] = dst_raw & 0xFF;
    dst.bytes[1] = (dst_raw >> 8) & 0xFF;
    dst.bytes[2] = (dst_raw >> 16) & 0xFF;
    dst.bytes[3] = (dst_raw >> 24) & 0xFF;
    
    terminal_print(term, "PING ");
    terminal_print(term, argv[1]);
    terminal_println(term, " ...");
    
    /* Send 4 pings */
    int success = 0;
    for (uint16_t seq = 1; seq <= 4; seq++) {
        int ret = icmp_ping(iface, dst, seq);
        if (ret == 0) {
            terminal_print(term, "  Reply from ");
            terminal_print(term, argv[1]);
            terminal_print(term, ": seq=");
            char seq_str[8];
            int_to_str(seq, seq_str, 8);
            terminal_print(term, seq_str);
            terminal_println(term, "");
            success++;
        } else {
            terminal_println(term, "  Request timed out");
        }
        /* Brief delay between pings */
        for (volatile int i = 0; i < 1000000; i++);
    }
    
    terminal_print(term, "--- ");
    char stat[32];
    int_to_str(success, stat, 32);
    terminal_print(term, stat);
    terminal_println(term, "/4 packets received ---");
    
    return TERM_OK;
}

/* dhcp - Request IP via DHCP */
extern int network_dhcp_request(void *iface);

term_status_t cmd_dhcp(terminal_state_t *term, int argc, char **argv) {
    (void)argc; (void)argv;
    
    /* Get default interface */
    void *iface = network_get_interface("eth0");
    if (!iface) {
        terminal_println(term, "dhcp: no network interface available");
        return TERM_ERR_IO;
    }
    
    terminal_println(term, "Sending DHCP DISCOVER...");
    terminal_println(term, "(Check serial console for DHCP progress)");
    
    int result = network_dhcp_request(iface);
    if (result < 0) {
        terminal_println(term, "DHCP: failed to send DISCOVER");
        return TERM_ERR_IO;
    }
    
    terminal_println(term, "DHCP request sent (waiting for OFFER/ACK)");
    return TERM_OK;
}

/* history - Show command history */
term_status_t cmd_history(terminal_state_t *term, int argc, char **argv) {
    terminal_println(term, "Command History:");
    for (int i = 0; i < term->history_count; i++) {
        terminal_print(term, "  ");
        terminal_println(term, term->history[i]);
    }
    return TERM_OK;
}

/* stress - Run kernel stress tests */
extern int stress_test_run(void);

term_status_t cmd_stress(terminal_state_t *term, int argc, char **argv) {
    (void)argc; (void)argv;  /* Suppress unused warnings */
    terminal_println(term, "Running kernel stress tests...");
    terminal_println(term, "(Output goes to serial console)");
    terminal_println(term, "");
    
    int failed = stress_test_run();
    
    if (failed == 0) {
        terminal_println(term, "All stress tests PASSED!");
    } else {
        terminal_print(term, "Stress tests FAILED: ");
        /* Simple number print */
        char buf[16];
        int i = 15;
        buf[i--] = '\0';
        int v = failed;
        if (v == 0) buf[i--] = '0';
        while (v > 0) { buf[i--] = '0' + (v % 10); v /= 10; }
        terminal_print(term, &buf[i + 1]);
        terminal_println(term, " failures");
    }
    return TERM_OK;
}

/* ============ Terminal Customization ============ */

/* Current terminal settings (global state) */
static struct {
    char theme[32];
    char font_name[32];
    uint8_t font_size;
    uint8_t transparency;
    uint8_t transparency_enabled;
} g_term_settings = {
    .theme = "dark",
    .font_name = "Consolas",
    .font_size = 14,
    .transparency = 100,
    .transparency_enabled = 0
};

/* sys theme list - List available themes */
term_status_t cmd_sys_theme_list(terminal_state_t *term, int argc, char **argv) {
    (void)argc; (void)argv;
    terminal_println(term, "Available Themes:");
    terminal_println(term, "  dark     - Dark theme (default)");
    terminal_println(term, "  light    - Light theme");
    terminal_println(term, "  retro    - Retro green-on-black");
    terminal_println(term, "  ocean    - Blue ocean theme");
    terminal_println(term, "  high-contrast - High contrast");
    terminal_println(term, "");
    terminal_print(term, "Current theme: ");
    terminal_println(term, g_term_settings.theme);
    return TERM_OK;
}

/* sys theme set <name> - Set terminal theme */
term_status_t cmd_sys_theme_set(terminal_state_t *term, int argc, char **argv) {
    if (argc < 3) {
        terminal_println(term, "usage: sys theme set <name>");
        terminal_println(term, "  available: dark, light, retro, ocean, high-contrast");
        return TERM_ERR_INVALID_ARGS;
    }
    
    const char *theme = argv[2];
    
    /* Validate theme name */
    if (str_cmp(theme, "dark") || str_cmp(theme, "light") || 
        str_cmp(theme, "retro") || str_cmp(theme, "ocean") ||
        str_cmp(theme, "high-contrast")) {
        str_copy(g_term_settings.theme, theme, 32);
        terminal_print(term, "Theme set to: ");
        terminal_println(term, theme);
        terminal_println(term, "(Theme will apply on next refresh)");
    } else {
        terminal_print(term, "Unknown theme: ");
        terminal_println(term, theme);
        terminal_println(term, "Use 'sys theme list' to see available themes.");
        return TERM_ERR_NOT_FOUND;
    }
    
    return TERM_OK;
}

/* sys theme reset - Reset to default theme */
term_status_t cmd_sys_theme_reset(terminal_state_t *term, int argc, char **argv) {
    (void)argc; (void)argv;
    str_copy(g_term_settings.theme, "dark", 32);
    terminal_println(term, "Theme reset to: dark");
    return TERM_OK;
}

/* sys font list - List available fonts */
term_status_t cmd_sys_font_list(terminal_state_t *term, int argc, char **argv) {
    (void)argc; (void)argv;
    terminal_println(term, "Available Fonts:");
    terminal_println(term, "  Consolas     - Default monospace");
    terminal_println(term, "  Monaco       - macOS-style");
    terminal_println(term, "  Menlo        - Clean monospace");
    terminal_println(term, "  Noto Mono    - Unicode support");
    terminal_println(term, "  Terminus     - Bitmap font");
    terminal_println(term, "");
    terminal_print(term, "Current font: ");
    terminal_print(term, g_term_settings.font_name);
    terminal_print(term, " ");
    char buf[8];
    int_to_str(g_term_settings.font_size, buf, 8);
    terminal_print(term, buf);
    terminal_println(term, "pt");
    return TERM_OK;
}

/* sys font set <name> <size> - Set terminal font */
term_status_t cmd_sys_font_set(terminal_state_t *term, int argc, char **argv) {
    if (argc < 3) {
        terminal_println(term, "usage: sys font set <name> [size]");
        terminal_println(term, "  example: sys font set Menlo 14");
        return TERM_ERR_INVALID_ARGS;
    }
    
    str_copy(g_term_settings.font_name, argv[2], 32);
    
    if (argc >= 4) {
        /* Parse size */
        int size = 0;
        const char *s = argv[3];
        while (*s >= '0' && *s <= '9') {
            size = size * 10 + (*s - '0');
            s++;
        }
        if (size >= 8 && size <= 32) {
            g_term_settings.font_size = size;
        }
    }
    
    terminal_print(term, "Font set to: ");
    terminal_print(term, g_term_settings.font_name);
    terminal_print(term, " ");
    char buf[8];
    int_to_str(g_term_settings.font_size, buf, 8);
    terminal_print(term, buf);
    terminal_println(term, "pt");
    
    return TERM_OK;
}

/* sys font size <size> - Change font size only */
term_status_t cmd_sys_font_size(terminal_state_t *term, int argc, char **argv) {
    if (argc < 3) {
        terminal_println(term, "usage: sys font size <size>");
        terminal_println(term, "  size: 8-32");
        return TERM_ERR_INVALID_ARGS;
    }
    
    int size = 0;
    const char *s = argv[2];
    while (*s >= '0' && *s <= '9') {
        size = size * 10 + (*s - '0');
        s++;
    }
    
    if (size < 8 || size > 32) {
        terminal_println(term, "error: size must be 8-32");
        return TERM_ERR_INVALID_ARGS;
    }
    
    g_term_settings.font_size = size;
    
    terminal_print(term, "Font size set to: ");
    char buf[8];
    int_to_str(size, buf, 8);
    terminal_print(term, buf);
    terminal_println(term, "pt");
    
    return TERM_OK;
}

/* sys transparency on/off/<value> - Control window transparency */
term_status_t cmd_sys_transparency(terminal_state_t *term, int argc, char **argv) {
    if (argc < 2) {
        terminal_println(term, "usage: sys transparency <on|off|0-100>");
        terminal_print(term, "  current: ");
        if (g_term_settings.transparency_enabled) {
            char buf[8];
            int_to_str(g_term_settings.transparency, buf, 8);
            terminal_print(term, buf);
            terminal_println(term, "%");
        } else {
            terminal_println(term, "off");
        }
        return TERM_OK;
    }
    
    const char *val = argv[1];
    
    if (str_cmp(val, "on")) {
        g_term_settings.transparency_enabled = 1;
        terminal_println(term, "Transparency enabled");
    } else if (str_cmp(val, "off")) {
        g_term_settings.transparency_enabled = 0;
        terminal_println(term, "Transparency disabled");
    } else {
        /* Parse numeric value */
        int v = 0;
        const char *s = val;
        while (*s >= '0' && *s <= '9') {
            v = v * 10 + (*s - '0');
            s++;
        }
        if (v >= 0 && v <= 100) {
            g_term_settings.transparency = v;
            g_term_settings.transparency_enabled = 1;
            terminal_print(term, "Transparency set to: ");
            char buf[8];
            int_to_str(v, buf, 8);
            terminal_print(term, buf);
            terminal_println(term, "%");
        } else {
            terminal_println(term, "error: value must be 0-100");
            return TERM_ERR_INVALID_ARGS;
        }
    }
    
    return TERM_OK;
}

/* ============ User Program Execution ============ */

/* External kernel functions for program execution */
extern int elf_load_file(const char *path, void *info);
extern int process_exec(const char *path, char *const argv[], char *const envp[]);
extern int process_fork(void);
extern int process_wait(int *status);
extern void serial_puts(const char *s);

/* exec - Execute a user program */
term_status_t cmd_exec(terminal_state_t *term, int argc, char **argv) {
    if (argc < 2) {
        terminal_println(term, "NeolyxOS Program Execution");
        terminal_println(term, "");
        terminal_println(term, "usage: exec <path> [args...]");
        terminal_println(term, "");
        terminal_println(term, "Loads and executes an ELF64 executable.");
        terminal_println(term, "");
        terminal_println(term, "Examples:");
        terminal_println(term, "  exec /bin/hello");
        terminal_println(term, "  exec ./myapp arg1 arg2");
        return TERM_ERR_INVALID_ARGS;
    }
    
    /* Build full path */
    char path[256];
    if (argv[1][0] == '/') {
        /* Absolute path */
        str_copy(path, argv[1], sizeof(path));
    } else if (str_starts_with(argv[1], "./")) {
        /* Relative to cwd */
        str_copy(path, term->cwd, sizeof(path));
        if (!str_cmp(term->cwd, "/")) {
            str_cat(path, "/", sizeof(path));
        }
        str_cat(path, &argv[1][2], sizeof(path));  /* Skip "./" */
    } else {
        /* Try in cwd first */
        str_copy(path, term->cwd, sizeof(path));
        if (!str_cmp(term->cwd, "/")) {
            str_cat(path, "/", sizeof(path));
        }
        str_cat(path, argv[1], sizeof(path));
    }
    
    terminal_print(term, "Loading: ");
    terminal_println(term, path);
    
    /* Check if file exists via VFS */
    vfs_file_t *file = vfs_open(path, VFS_O_RDONLY);
    if (!file) {
        terminal_print(term, "error: file not found: ");
        terminal_println(term, path);
        return TERM_ERR_NOT_FOUND;
    }
    vfs_close(file);
    
    terminal_println(term, "Executing program...");
    terminal_println(term, "");
    
    /*
     * Execute the program using fork/exec pattern:
     * 1. Fork creates child process
     * 2. Child calls exec to load program
     * 3. Parent waits for child to complete
     * 
     * Note: In current kernel state, we run directly without
     * Ring 3 transition for simplicity during development.
     */
    
    /* Build argv array for child process */
    char *child_argv[16];
    int child_argc = 0;
    
    for (int i = 1; i < argc && child_argc < 15; i++) {
        child_argv[child_argc++] = argv[i];
    }
    child_argv[child_argc] = 0;  /* NULL terminate */
    
    serial_puts("[EXEC] Attempting to run: ");
    serial_puts(path);
    serial_puts("\n");
    
    /* Execute the program */
    int result = process_exec(path, child_argv, 0);
    
    if (result != 0) {
        terminal_println(term, "");
        terminal_print(term, "error: execution failed (code ");
        
        /* Simple number formatting */
        char buf[16];
        int i = 15;
        buf[i--] = '\0';
        int v = result < 0 ? -result : result;
        if (v == 0) buf[i--] = '0';
        while (v > 0) { buf[i--] = '0' + (v % 10); v /= 10; }
        if (result < 0) buf[i--] = '-';
        terminal_print(term, &buf[i + 1]);
        
        terminal_println(term, ")");
        return TERM_ERR_PERMISSION;
    }
    
    terminal_println(term, "");
    terminal_println(term, "Program completed.");
    return TERM_OK;
}

/* run - Alias for exec */
term_status_t cmd_run(terminal_state_t *term, int argc, char **argv) {
    return cmd_exec(term, argc, argv);
}

/* ============ NPA Package Manager Commands ============ */

/* npa - Show help when called without subcommand */
term_status_t cmd_npa_help(terminal_state_t *term, int argc, char **argv) {
    (void)argc; (void)argv;
    
    terminal_println(term, "NPA Package Manager - NeolyxOS");
    terminal_println(term, "");
    terminal_println(term, "usage: npa <command> [args]");
    terminal_println(term, "");
    terminal_println(term, "Commands:");
    terminal_println(term, "  install <path>   Install package from .npa file");
    terminal_println(term, "  remove <name>    Remove installed package");
    terminal_println(term, "  list             List installed packages");
    terminal_println(term, "  search <term>    Search packages by name");
    terminal_println(term, "  update <path>    Update package from .npa file");
    terminal_println(term, "  info <name>      Show package details");
    terminal_println(term, "  verify <name>    Verify package integrity");
    terminal_println(term, "");
    terminal_println(term, "Package files use .npa extension.");
    terminal_println(term, "Packages install to /apps/packages/<name>/");
    
    return TERM_OK;
}

/* npa install <path> */
term_status_t cmd_npa_install(terminal_state_t *term, int argc, char **argv) {
    if (argc < 3) {
        terminal_println(term, "usage: npa install <path.npa>");
        return TERM_ERR_INVALID_ARGS;
    }
    
    const char *path = argv[2];
    
    /* Build full path if relative */
    char full_path[256];
    if (path[0] == '/') {
        str_copy(full_path, path, 256);
    } else {
        str_copy(full_path, term->cwd, 256);
        if (!str_cmp(term->cwd, "/")) {
            str_cat(full_path, "/", 256);
        }
        str_cat(full_path, path, 256);
    }
    
    terminal_print(term, "Installing package from: ");
    terminal_println(term, full_path);
    
    int result = npa_install(full_path);
    
    if (result == 0) {
        terminal_println(term, "Package installed successfully!");
    } else if (result == -5) {
        terminal_println(term, "Package already installed. Use 'npa update' to upgrade.");
    } else {
        terminal_print(term, "Installation failed (error ");
        char buf[16];
        int_to_str((result < 0 ? -result : result), buf, 16);
        terminal_print(term, "-");
        terminal_print(term, buf);
        terminal_println(term, ")");
    }
    
    return result == 0 ? TERM_OK : TERM_ERR_IO;
}

/* npa remove <name> */
term_status_t cmd_npa_remove(terminal_state_t *term, int argc, char **argv) {
    if (argc < 3) {
        terminal_println(term, "usage: npa remove <package-name>");
        return TERM_ERR_INVALID_ARGS;
    }
    
    const char *name = argv[2];
    
    terminal_print(term, "Removing package: ");
    terminal_println(term, name);
    
    int result = npa_remove(name);
    
    if (result == 0) {
        terminal_println(term, "Package removed successfully!");
    } else if (result == -2) {
        terminal_println(term, "Package not found.");
    } else {
        terminal_println(term, "Removal failed.");
    }
    
    return result == 0 ? TERM_OK : TERM_ERR_NOT_FOUND;
}

/* npa list */
term_status_t cmd_npa_list(terminal_state_t *term, int argc, char **argv) {
    (void)argc; (void)argv;
    
    npa_package_info_t packages[32];
    int count = 0;
    
    npa_list(packages, 32, &count);
    
    terminal_println(term, "Installed Packages:");
    terminal_println(term, "");
    
    if (count == 0) {
        terminal_println(term, "  (none)");
    } else {
        for (int i = 0; i < count; i++) {
            terminal_print(term, "  ");
            terminal_print(term, packages[i].name);
            terminal_print(term, " v");
            terminal_print(term, packages[i].version);
            terminal_println(term, "");
        }
    }
    
    terminal_println(term, "");
    char buf[16];
    int_to_str(count, buf, 16);
    terminal_print(term, buf);
    terminal_println(term, " package(s) installed.");
    
    return TERM_OK;
}

/* npa search <term> */
term_status_t cmd_npa_search(terminal_state_t *term, int argc, char **argv) {
    if (argc < 3) {
        terminal_println(term, "usage: npa search <term>");
        return TERM_ERR_INVALID_ARGS;
    }
    
    const char *query = argv[2];
    
    npa_package_info_t results[16];
    int count = 0;
    
    npa_search(query, results, 16, &count);
    
    terminal_print(term, "Search results for '");
    terminal_print(term, query);
    terminal_println(term, "':");
    terminal_println(term, "");
    
    if (count == 0) {
        terminal_println(term, "  No matches found.");
    } else {
        for (int i = 0; i < count; i++) {
            terminal_print(term, "  ");
            terminal_print(term, results[i].name);
            terminal_print(term, " - ");
            terminal_println(term, results[i].description);
        }
    }
    
    return TERM_OK;
}

/* npa update <path> */
term_status_t cmd_npa_update(terminal_state_t *term, int argc, char **argv) {
    if (argc < 3) {
        terminal_println(term, "usage: npa update <path.npa>");
        terminal_println(term, "       npa update all");
        return TERM_ERR_INVALID_ARGS;
    }
    
    const char *arg = argv[2];
    
    if (str_cmp(arg, "all")) {
        terminal_println(term, "Checking for updates...");
        int updates = npa_check_updates();
        
        if (updates == 0) {
            terminal_println(term, "No updates available.");
        } else {
            char buf[16];
            int_to_str(updates, buf, 16);
            terminal_print(term, buf);
            terminal_println(term, " update(s) found. Applying...");
            
            int applied = npa_apply_all_updates();
            int_to_str(applied, buf, 16);
            terminal_print(term, buf);
            terminal_println(term, " update(s) applied.");
        }
    } else {
        /* Update from specific file */
        char full_path[256];
        if (arg[0] == '/') {
            str_copy(full_path, arg, 256);
        } else {
            str_copy(full_path, term->cwd, 256);
            if (!str_cmp(term->cwd, "/")) {
                str_cat(full_path, "/", 256);
            }
            str_cat(full_path, arg, 256);
        }
        
        terminal_print(term, "Updating from: ");
        terminal_println(term, full_path);
        
        int result = npa_apply_update(full_path);
        
        if (result == 0) {
            terminal_println(term, "Update applied successfully!");
        } else {
            terminal_println(term, "Update failed.");
        }
    }
    
    return TERM_OK;
}

/* npa info <name> */
term_status_t cmd_npa_info(terminal_state_t *term, int argc, char **argv) {
    if (argc < 3) {
        terminal_println(term, "usage: npa info <package-name>");
        return TERM_ERR_INVALID_ARGS;
    }
    
    const char *name = argv[2];
    
    npa_package_info_t info;
    int result = npa_info(name, &info);
    
    if (result != 0) {
        terminal_print(term, "Package not found: ");
        terminal_println(term, name);
        return TERM_ERR_NOT_FOUND;
    }
    
    terminal_println(term, "Package Information:");
    terminal_println(term, "");
    
    terminal_print(term, "  Name:        ");
    terminal_println(term, info.name);
    
    terminal_print(term, "  Version:     ");
    terminal_println(term, info.version);
    
    terminal_print(term, "  Description: ");
    terminal_println(term, info.description);
    
    terminal_print(term, "  Author:      ");
    terminal_println(term, info.author);
    
    terminal_print(term, "  Location:    ");
    terminal_println(term, info.install_path);
    
    terminal_print(term, "  Files:       ");
    char buf[16];
    int_to_str(info.file_count, buf, 16);
    terminal_println(term, buf);
    
    terminal_print(term, "  Status:      ");
    switch (info.status) {
        case NPA_STATUS_INSTALLED:
            terminal_println(term, "Installed");
            break;
        case NPA_STATUS_BROKEN:
            terminal_println(term, "Broken");
            break;
        default:
            terminal_println(term, "Unknown");
    }
    
    return TERM_OK;
}

/* npa verify <name> */
term_status_t cmd_npa_verify(terminal_state_t *term, int argc, char **argv) {
    if (argc < 3) {
        terminal_println(term, "usage: npa verify <package-name>");
        return TERM_ERR_INVALID_ARGS;
    }
    
    const char *name = argv[2];
    
    terminal_print(term, "Verifying package: ");
    terminal_println(term, name);
    
    int result = npa_verify(name);
    
    if (result == 0) {
        terminal_println(term, "Package integrity verified OK.");
    } else if (result == -1) {
        terminal_println(term, "Package not found.");
    } else {
        terminal_println(term, "Package integrity FAILED - files may be corrupted.");
    }
    
    return result == 0 ? TERM_OK : TERM_ERR_NOT_FOUND;
}

/* ============ Command Aliases ============ */

/* mkdir - alias for 'make dir' */
term_status_t cmd_mkdir_alias(terminal_state_t *term, int argc, char **argv) {
    if (argc < 2) {
        terminal_println(term, "error: mkdir requires a directory name");
        terminal_println(term, "usage: mkdir <dirname>");
        return TERM_ERR_INVALID_ARGS;
    }
    char *new_argv[] = {"make", "dir", argv[1]};
    return cmd_make(term, 3, new_argv);
}

/* touch - alias for creating empty file */
term_status_t cmd_touch_alias(terminal_state_t *term, int argc, char **argv) {
    if (argc < 2) {
        terminal_println(term, "error: touch requires a filename");
        terminal_println(term, "usage: touch <filename>");
        return TERM_ERR_INVALID_ARGS;
    }
    char *new_argv[] = {"make", "file", argv[1]};
    return cmd_make(term, 3, new_argv);
}

/* ============ Command Dispatch Table ============ */

static const command_t commands[] = {
    /* Shell utilities */
    {"help",     NULL,       0, "Show available commands",    cmd_help},
    {"clear",    NULL,       0, "Clear terminal screen",      cmd_clear},
    {"exit",     NULL,       0, "Exit terminal",              cmd_exit},
    {"version",  NULL,       0, "Show OS version",            cmd_version},
    {"history",  NULL,       0, "Show command history",       cmd_history},
    
    /* Info commands */
    {"info",     "system",   0, "System overview",            cmd_info_system},
    {"info",     "memory",   0, "Memory usage",               cmd_info_memory},
    {"info",     "disk",     0, "Disk usage",                 cmd_info_disk},
    {"info",     "cpu",      0, "CPU information",            cmd_info_cpu},
    {"uptime",   NULL,       0, "System uptime",              cmd_uptime},
    
    /* File operations */
    {"pwd",      NULL,       0, "Print working directory",    cmd_pwd},
    {"cd",       NULL,       0, "Change directory",           cmd_cd},
    {"list",     NULL,       0, "List directory contents",    cmd_list},
    {"ls",       NULL,       0, "List files (alias)",         cmd_list},
    {"dir",      NULL,       0, "List files (alias)",         cmd_list},
    {"show",     NULL,       0, "Display file contents",      cmd_show},
    {"cat",      NULL,       0, "Display file (alias)",       cmd_show},
    {"make",     "file",     0, "Create new file",            cmd_make},
    {"make",     "dir",      0, "Create new directory",       cmd_make},
    {"mkdir",    NULL,       0, "Create directory (alias)",   cmd_mkdir_alias},
    {"touch",    NULL,       0, "Create file (alias)",        cmd_touch_alias},
    {"remove",   NULL,       0, "Delete file (user only)",    cmd_remove},
    {"rm",       NULL,       0, "Delete file (alias)",        cmd_remove},
    
    /* Process control */
    {"tasks",    NULL,       0, "Show running tasks",         cmd_tasks},
    {"task",     NULL,       0, "Show tasks (alias)",         cmd_tasks},
    {"ps",       NULL,       0, "Show tasks (alias)",         cmd_tasks},
    {"exec",     NULL,       0, "Execute user program",       cmd_exec},
    {"run",      NULL,       0, "Run user program (alias)",   cmd_run},
    
    /* Network */
    {"net",      "status",   0, "Network status",             cmd_net_status},
    {"ping",     NULL,       0, "Test network connectivity",  cmd_ping},
    {"dhcp",     NULL,       0, "Request IP via DHCP",        cmd_dhcp},
    
    /* Sys commands (require sys prefix) */
    {"status",   NULL,       1, "Show privilege level",       cmd_sys_status},
    {"reboot",   NULL,       1, "Restart system",             cmd_sys_reboot},
    {"shutdown", NULL,       1, "Power off system",           cmd_sys_shutdown},
    
    /* Terminal customization (sys prefix) */
    {"theme",    "list",     1, "List themes",                cmd_sys_theme_list},
    {"theme",    "set",      1, "Set theme",                  cmd_sys_theme_set},
    {"theme",    "reset",    1, "Reset theme",                cmd_sys_theme_reset},
    {"font",     "list",     1, "List fonts",                 cmd_sys_font_list},
    {"font",     "set",      1, "Set font",                   cmd_sys_font_set},
    {"font",     "size",     1, "Set font size",              cmd_sys_font_size},
    {"transparency", NULL,   1, "Window transparency",        cmd_sys_transparency},
    
    /* Debug/Test commands */
    {"stress",   NULL,       1, "Run stress tests",           cmd_stress},
    {"test",     NULL,       0, "Run kernel test suite",      cmd_test},
    
    /* NeolyxOS App Framework */
    {"nxapp",    "create",   0, "Create new app scaffold",    cmd_nxapp_create},
    
    /* NPA Package Manager */
    {"npa",      "install",  0, "Install package",            cmd_npa_install},
    {"npa",      "remove",   0, "Remove package",             cmd_npa_remove},
    {"npa",      "list",     0, "List packages",              cmd_npa_list},
    {"npa",      "search",   0, "Search packages",            cmd_npa_search},
    {"npa",      "update",   0, "Update package",             cmd_npa_update},
    {"npa",      "info",     0, "Package info",               cmd_npa_info},
    {"npa",      "verify",   0, "Verify package",             cmd_npa_verify},
    {"npa",      NULL,       0, "Package manager help",       cmd_npa_help},
    
    /* End marker */
    {NULL, NULL, 0, NULL, NULL}
};

/* ============ Terminal Core ============ */

void terminal_init(terminal_state_t *term) {
    str_copy(term->cwd, "/", TERM_CWD_MAX);
    str_copy(term->username, "user", TERM_USERNAME_MAX);
    term->privilege = PRIV_USER;
    term->input[0] = '\0';
    term->input_pos = 0;
    term->history_count = 0;
    term->history_pos = 0;
    term->output[0] = '\0';
    term->output_len = 0;
    term->running = 1;
}

void terminal_print(terminal_state_t *term, const char *str) {
    str_cat(term->output, str, TERM_OUTPUT_MAX);
    term->output_len = str_len(term->output);
}

void terminal_println(terminal_state_t *term, const char *str) {
    terminal_print(term, str);
    terminal_print(term, "\n");
}

void terminal_clear_output(terminal_state_t *term) {
    term->output[0] = '\0';
    term->output_len = 0;
}

/* Parse input into argv array */
int terminal_parse(const char *input, char **argv, int max_args) {
    static char buffer[TERM_INPUT_MAX];
    str_copy(buffer, input, TERM_INPUT_MAX);
    
    int argc = 0;
    char *p = buffer;
    
    while (*p && argc < max_args) {
        /* Skip whitespace */
        while (*p == ' ') p++;
        if (!*p) break;
        
        /* Start of token */
        argv[argc++] = p;
        
        /* Find end of token */
        while (*p && *p != ' ') p++;
        if (*p) *p++ = '\0';
    }
    
    return argc;
}

/* Look up command in table */
const command_t* terminal_lookup(const char *cmd, const char *subcmd) {
    for (int i = 0; commands[i].name != NULL; i++) {
        if (str_cmp(commands[i].name, cmd)) {
            if (commands[i].subcommand == NULL && subcmd == NULL) {
                return &commands[i];
            }
            if (commands[i].subcommand && subcmd && str_cmp(commands[i].subcommand, subcmd)) {
                return &commands[i];
            }
        }
    }
    return NULL;
}

/* Execute command */
term_status_t terminal_execute(terminal_state_t *term) {
    char *argv[TERM_ARG_MAX];
    int argc = terminal_parse(term->input, argv, TERM_ARG_MAX);
    
    if (argc == 0) {
        return TERM_OK;  /* Empty command */
    }
    
    /* Add to history */
    if (term->history_count < TERM_HISTORY_MAX) {
        str_copy(term->history[term->history_count], term->input, TERM_INPUT_MAX);
        term->history_count++;
    }
    
    /* Check for sys prefix */
    int cmd_start = 0;
    term->privilege = PRIV_USER;
    
    if (str_cmp(argv[0], "sys")) {
        term->privilege = PRIV_SYSTEM;
        cmd_start = 1;
        if (argc == 1) {
            terminal_println(term, "error: sys requires a command");
            terminal_println(term, "usage: sys <command>");
            return TERM_ERR_INVALID_ARGS;
        }
    }
    
    /* Get command and subcommand */
    const char *cmd = (cmd_start < argc) ? argv[cmd_start] : NULL;
    const char *subcmd = (cmd_start + 1 < argc) ? argv[cmd_start + 1] : NULL;
    
    if (!cmd) {
        return TERM_OK;
    }
    
    /* Look up command */
    const command_t *found = terminal_lookup(cmd, subcmd);
    
    /* Try without subcommand if not found */
    if (!found && subcmd) {
        found = terminal_lookup(cmd, NULL);
    }
    
    if (!found) {
        terminal_print(term, "error: unknown command '");
        terminal_print(term, cmd);
        terminal_println(term, "'");
        terminal_println(term, "type 'help' to list commands");
        return TERM_ERR_UNKNOWN_CMD;
    }
    
    /* Check permission */
    if (found->requires_sys && term->privilege != PRIV_SYSTEM) {
        terminal_print(term, "error: '");
        terminal_print(term, cmd);
        terminal_println(term, "' requires sys prefix");
        return TERM_ERR_PERMISSION;
    }
    
    /* Execute handler */
    int handler_argc = argc - cmd_start;
    char **handler_argv = &argv[cmd_start];
    
    return found->handler(term, handler_argc, handler_argv);
}

/* Get prompt string */
const char* terminal_get_prompt(terminal_state_t *term) {
    static char prompt[TERM_CWD_MAX + 32];
    prompt[0] = '\0';
    str_cat(prompt, term->username, sizeof(prompt));
    str_cat(prompt, "@neolyx:", sizeof(prompt));
    str_cat(prompt, term->cwd, sizeof(prompt));
    str_cat(prompt, "$ ", sizeof(prompt));
    return prompt;
}

/* Handle scancode input */
void terminal_input_scancode(terminal_state_t *term, uint8_t scancode) {
    /* Scancode to char map (basic) */
    static const char keymap[] = {
        0, 0, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', 0, 0,
        'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', 0, 0,
        'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\',
        'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, 0, 0, ' '
    };
    
    if (scancode == 0x0E && term->input_pos > 0) {
        /* Backspace */
        term->input_pos--;
        term->input[term->input_pos] = '\0';
    } else if (scancode == 0x1C) {
        /* Enter - execute */
        terminal_execute(term);
        term->input[0] = '\0';
        term->input_pos = 0;
    } else if (scancode < sizeof(keymap) && keymap[scancode]) {
        /* Regular character */
        if (term->input_pos < TERM_INPUT_MAX - 1) {
            term->input[term->input_pos++] = keymap[scancode];
            term->input[term->input_pos] = '\0';
        }
    }
}

void terminal_input_char(terminal_state_t *term, char c) {
    if (c == '\b' && term->input_pos > 0) {
        term->input_pos--;
        term->input[term->input_pos] = '\0';
    } else if (c == '\n' || c == '\r') {
        terminal_execute(term);
        term->input[0] = '\0';
        term->input_pos = 0;
    } else if (c >= 32 && c < 127) {
        if (term->input_pos < TERM_INPUT_MAX - 1) {
            term->input[term->input_pos++] = c;
            term->input[term->input_pos] = '\0';
        }
    }
}
