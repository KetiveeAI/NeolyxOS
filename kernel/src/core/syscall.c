/*
 * NeolyxOS System Call Implementation
 * 
 * C dispatch handler and syscall implementations.
 * 
 * Copyright (c) 2025 KetiveeAI.
 */

#include "core/syscall.h"
#include "core/process.h"
#include "core/panic.h"
#include "fs/vfs.h"
#include "mm/kheap.h"
#include "mm/pmm.h"

/* ============ External Dependencies ============ */

extern void serial_puts(const char *s);
extern void serial_putc(char c);
extern void syscall_msr_init(void);
extern void syscall_enable(void);
extern uint64_t pit_get_ticks(void);

/* ============ Syscall Table ============ */

typedef int64_t (*syscall_fn_t)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
static syscall_fn_t syscall_table[SYS_MAX];

/* Privilege requirements */
static uint8_t syscall_priv[SYS_MAX];

/* ============ Helpers ============ */

static void serial_dec(int64_t val) {
    if (val < 0) { serial_putc('-'); val = -val; }
    char buf[21];
    int i = 20;
    buf[i--] = '\0';
    if (val == 0) { serial_putc('0'); return; }
    while (val > 0) { buf[i--] = '0' + (val % 10); val /= 10; }
    serial_puts(&buf[i + 1]);
}

/* String length helper - reserved for future syscall string handling */
static int sc_strlen(const char *s) __attribute__((unused));
static int sc_strlen(const char *s) {
    int len = 0;
    while (s[len]) len++;
    return len;
}

static void sc_strcpy(char *dst, const char *src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

/* ============ User Memory Validation ============ */

int syscall_validate_ptr(const void *ptr, size_t size, int write) {
    if (!ptr) return -EFAULT;
    
    /* Check if pointer is in user space (below kernel) */
    uint64_t addr = (uint64_t)ptr;
    
    /* Simple check: user space is below 0x0000800000000000 */
    if (addr >= 0x0000800000000000ULL) {
        return -EFAULT;
    }
    
    /* TODO: Check page tables for actual mapping */
    (void)size;
    (void)write;
    
    return 0;
}

int syscall_copy_from_user(void *dst, const void *src, size_t size) {
    if (syscall_validate_ptr(src, size, 0) != 0) {
        return -EFAULT;
    }
    
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    while (size--) *d++ = *s++;
    
    return 0;
}

int syscall_copy_to_user(void *dst, const void *src, size_t size) {
    if (syscall_validate_ptr(dst, size, 1) != 0) {
        return -EFAULT;
    }
    
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    while (size--) *d++ = *s++;
    
    return 0;
}

int syscall_strncpy_from_user(char *dst, const char *src, size_t max) {
    if (syscall_validate_ptr(src, 1, 0) != 0) {
        return -EFAULT;
    }
    
    size_t i = 0;
    while (i < max - 1) {
        dst[i] = src[i];
        if (src[i] == '\0') break;
        i++;
    }
    dst[i] = '\0';
    return i;
}

/* ============ Privilege Check ============ */

static int current_priv = PRIV_USER;

int syscall_has_priv(int level) {
    return current_priv >= level;
}

int syscall_elevate(void) {
    /* TODO: Implement proper elevation check */
    current_priv = PRIV_SYSTEM;
    return 0;
}

/* ============ Syscall Implementations ============ */

/* Process Control */

static int64_t sys_exit_impl(uint64_t code, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5) {
    (void)a2; (void)a3; (void)a4; (void)a5;
    process_exit((int)code);
    return 0;  /* Never reached */
}

static int64_t sys_sleep_impl(uint64_t ms, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5) {
    (void)a2; (void)a3; (void)a4; (void)a5;
    
    /* Debug: trace first 5 sleep calls and every 500th */
    static int sleep_cnt = 0;
    sleep_cnt++;
    if (sleep_cnt <= 5 || sleep_cnt % 500 == 0) {
        serial_puts("[SLEEP] #");
        serial_dec(sleep_cnt);
        serial_puts(" ms=");
        serial_dec(ms);
        serial_puts(" ticks=");
        serial_dec(pit_get_ticks());
        serial_puts("\n");
    }
    
    process_sleep((uint32_t)ms);
    return 0;
}

static int64_t sys_time_impl(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5) {
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5;
    return pit_get_ticks();
}

static int64_t sys_yield_impl(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5) {
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5;
    /* TODO: Re-enable when desktop has proper process structure
     * For now, just return - the desktop was started directly with IRETQ
     * and has no process context for the scheduler to switch.
     */
    /* process_yield(); */
    return 0;
}

static int64_t sys_getpid_impl(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5) {
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5;
    process_t *proc = process_current();
    return proc ? proc->pid : -1;
}

static int64_t sys_getppid_impl(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5) {
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5;
    process_t *proc = process_current();
    return proc ? proc->ppid : -1;
}

static int64_t sys_fork_impl(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5) {
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5;
    return process_fork();
}

static int64_t sys_exec_impl(uint64_t path_ptr, uint64_t argv_ptr, uint64_t envp_ptr, uint64_t a4, uint64_t a5) {
    (void)a4; (void)a5;
    
    char path[256];
    if (syscall_strncpy_from_user(path, (const char *)path_ptr, 256) < 0) {
        return -EFAULT;
    }
    
    /* Note: argv and envp are user pointers - process_exec handles them */
    return process_exec(path, (char *const *)argv_ptr, (char *const *)envp_ptr);
}

static int64_t sys_wait_impl(uint64_t pid, uint64_t status_ptr, uint64_t a3, uint64_t a4, uint64_t a5) {
    (void)a3; (void)a4; (void)a5;
    
    int status = 0;
    int32_t result = process_wait((int32_t)pid, &status);
    
    if (result >= 0 && status_ptr) {
        if (syscall_copy_to_user((void *)status_ptr, &status, sizeof(int)) != 0) {
            return -EFAULT;
        }
    }
    
    return result;
}

/* Memory */

/* Global heap tracking for desktop (no process context)
 * This is set by the ELF loader after loading the desktop binary.
 * Windows/macOS style: 64MB heap reserve, grow via brk syscall */
static uint64_t g_desktop_heap_start = 0;
static uint64_t g_desktop_heap_end = 0;
static uint64_t g_desktop_heap_max = 0;  /* 64MB reserve limit */

void syscall_set_desktop_heap(uint64_t start, uint64_t max_size) {
    g_desktop_heap_start = start;
    g_desktop_heap_end = start;
    g_desktop_heap_max = start + max_size;
    serial_puts("[SYSCALL] Desktop heap: ");
    serial_dec(start >> 20);
    serial_puts("MB - ");
    serial_dec((start + max_size) >> 20);
    serial_puts("MB (");
    serial_dec(max_size >> 20);
    serial_puts("MB reserve)\n");
}

static int64_t sys_brk_impl(uint64_t addr, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5) {
    (void)a2; (void)a3; (void)a4; (void)a5;
    
    /* Use desktop global heap first if configured (desktop runs directly, no process) */
    if (g_desktop_heap_start != 0) {
        if (addr == 0) {
            serial_puts("[BRK] Returning TEST value 0xDEADBEEF\n");
            return 0xDEADBEEF;  /* TEST: hardcoded value to verify syscall return */
        }
        
        if (addr < g_desktop_heap_start) {
            return -EINVAL;  /* Can't shrink below start */
        }
        
        if (addr > g_desktop_heap_max) {
            serial_puts("[BRK] ERROR: Heap request exceeds 64MB reserve\n");
            return -ENOMEM;
        }
        
        /* Page-align the request */
        uint64_t aligned = (addr + 4095) & ~4095;
        
        /* Reserve pages with PMM for the new heap region */
        if (aligned > g_desktop_heap_end) {
            uint64_t pages_needed = (aligned - g_desktop_heap_end) / 4096;
            pmm_reserve_region(g_desktop_heap_end, pages_needed * 4096);
        }
        
        g_desktop_heap_end = aligned;
        return aligned;
    }
    
    /* Fall back to process-based heap (for future multi-process support) */
    process_t *proc = process_current();
    if (proc && proc->heap_start) {
        if (addr == 0) {
            return proc->heap_end;
        }
        if (addr < proc->heap_start) {
            return -EINVAL;
        }
        proc->heap_end = addr;
        return addr;
    }
    
    serial_puts("[BRK] ERROR: No heap configured\n");
    return -ENOMEM;
}

/* File I/O */

static vfs_file_t *fd_table[16] = {NULL};

static int64_t sys_open_impl(uint64_t path_ptr, uint64_t flags, uint64_t mode, uint64_t a4, uint64_t a5) {
    (void)mode; (void)a4; (void)a5;
    
    char path[256];
    if (syscall_strncpy_from_user(path, (const char *)path_ptr, 256) < 0) {
        return -EFAULT;
    }
    
    vfs_file_t *file = vfs_open(path, (uint32_t)flags);
    if (!file) return -ENOENT;
    
    /* Find free fd */
    for (int fd = 0; fd < 16; fd++) {
        if (fd_table[fd] == NULL) {
            fd_table[fd] = file;
            return fd;
        }
    }
    
    vfs_close(file);
    return -EMFILE;
}

static int64_t sys_close_impl(uint64_t fd, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5) {
    (void)a2; (void)a3; (void)a4; (void)a5;
    
    if (fd >= 16 || fd_table[fd] == NULL) {
        return -EBADF;
    }
    
    vfs_close(fd_table[fd]);
    fd_table[fd] = NULL;
    return 0;
}

static int64_t sys_read_impl(uint64_t fd, uint64_t buf_ptr, uint64_t count, uint64_t a4, uint64_t a5) {
    (void)a4; (void)a5;
    
    if (fd >= 16 || fd_table[fd] == NULL) {
        return -EBADF;
    }
    
    if (syscall_validate_ptr((void *)buf_ptr, count, 1) != 0) {
        return -EFAULT;
    }
    
    return vfs_read(fd_table[fd], (void *)buf_ptr, count);
}

static int64_t sys_write_impl(uint64_t fd, uint64_t buf_ptr, uint64_t count, uint64_t a4, uint64_t a5) {
    (void)a4; (void)a5;
    
    if (fd >= 16 || fd_table[fd] == NULL) {
        return -EBADF;
    }
    
    if (syscall_validate_ptr((void *)buf_ptr, count, 0) != 0) {
        return -EFAULT;
    }
    
    return vfs_write(fd_table[fd], (void *)buf_ptr, count);
}

static int64_t sys_seek_impl(uint64_t fd, uint64_t offset, uint64_t whence, uint64_t a4, uint64_t a5) {
    (void)a4; (void)a5;
    
    if (fd >= 16 || fd_table[fd] == NULL) {
        return -EBADF;
    }
    
    return vfs_seek(fd_table[fd], (int64_t)offset, (int)whence);
}

/* Current working directory (global for now, TODO: per-process) */
static char cwd[256] = "/";

static int64_t sys_stat_impl(uint64_t path_ptr, uint64_t stat_ptr, uint64_t a3, uint64_t a4, uint64_t a5) {
    (void)a3; (void)a4; (void)a5;
    
    char path[256];
    if (syscall_strncpy_from_user(path, (const char *)path_ptr, 256) < 0) {
        return -EFAULT;
    }
    
    vfs_node_t node;
    int ret = vfs_stat(path, &node);
    if (ret != 0) {
        return -ENOENT;
    }
    
    /* Convert to user stat structure */
    stat_t st;
    st.st_ino = node.inode;
    st.st_mode = node.permissions | (node.type == VFS_DIRECTORY ? 0040000 : 0100000);
    st.st_nlink = 1;
    st.st_uid = node.uid;
    st.st_gid = node.gid;
    st.st_size = node.size;
    st.st_atime = node.accessed;
    st.st_mtime = node.modified;
    st.st_ctime = node.created;
    
    if (syscall_copy_to_user((void *)stat_ptr, &st, sizeof(st)) != 0) {
        return -EFAULT;
    }
    
    return 0;
}

static int64_t sys_readdir_impl(uint64_t path_ptr, uint64_t entries_ptr, uint64_t count, uint64_t read_ptr, uint64_t a5) {
    (void)a5;
    
    char path[256];
    if (syscall_strncpy_from_user(path, (const char *)path_ptr, 256) < 0) {
        return -EFAULT;
    }
    
    if (syscall_validate_ptr((void *)entries_ptr, count * sizeof(vfs_dirent_t), 1) != 0) {
        return -EFAULT;
    }
    
    uint32_t nread = 0;
    int ret = vfs_readdir(path, (vfs_dirent_t *)entries_ptr, (uint32_t)count, &nread);
    
    if (read_ptr) {
        if (syscall_copy_to_user((void *)read_ptr, &nread, sizeof(uint32_t)) != 0) {
            return -EFAULT;
        }
    }
    
    return ret;
}

static int64_t sys_mkdir_impl(uint64_t path_ptr, uint64_t mode, uint64_t a3, uint64_t a4, uint64_t a5) {
    (void)a3; (void)a4; (void)a5;
    
    char path[256];
    if (syscall_strncpy_from_user(path, (const char *)path_ptr, 256) < 0) {
        return -EFAULT;
    }
    
    return vfs_create(path, VFS_DIRECTORY, (uint32_t)mode);
}

static int64_t sys_unlink_impl(uint64_t path_ptr, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5) {
    (void)a2; (void)a3; (void)a4; (void)a5;
    
    char path[256];
    if (syscall_strncpy_from_user(path, (const char *)path_ptr, 256) < 0) {
        return -EFAULT;
    }
    
    return vfs_unlink(path);
}

static int64_t sys_chdir_impl(uint64_t path_ptr, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5) {
    (void)a2; (void)a3; (void)a4; (void)a5;
    
    char path[256];
    if (syscall_strncpy_from_user(path, (const char *)path_ptr, 256) < 0) {
        return -EFAULT;
    }
    
    /* Verify path exists and is a directory */
    vfs_node_t node;
    int ret = vfs_stat(path, &node);
    if (ret != 0) {
        return -ENOENT;
    }
    if (node.type != VFS_DIRECTORY) {
        return -ENOTDIR;
    }
    
    /* Update cwd */
    sc_strcpy(cwd, path, 256);
    return 0;
}

static int64_t sys_getcwd_impl(uint64_t buf_ptr, uint64_t size, uint64_t a3, uint64_t a4, uint64_t a5) {
    (void)a3; (void)a4; (void)a5;
    
    if (syscall_validate_ptr((void *)buf_ptr, size, 1) != 0) {
        return -EFAULT;
    }
    
    int len = sc_strlen(cwd);
    if ((uint64_t)len + 1 > size) {
        return -ERANGE;
    }
    
    if (syscall_copy_to_user((void *)buf_ptr, cwd, len + 1) != 0) {
        return -EFAULT;
    }
    
    return len;
}

/* System Info */

static int64_t sys_info_impl(uint64_t info_ptr, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5) {
    (void)a2; (void)a3; (void)a4; (void)a5;
    
    sysinfo_t info;
    sc_strcpy(info.os_name, "NeolyxOS", 32);
    sc_strcpy(info.os_version, "1.0.0", 32);
    sc_strcpy(info.arch, "x86_64", 16);
    info.uptime_ms = pit_get_ticks();
    
    pmm_stats_t stats;
    pmm_get_stats(&stats);
    info.total_memory = stats.total_memory;
    info.free_memory = stats.free_memory;
    info.num_cpus = 1;
    info.num_processes = 1;  /* TODO: Get actual count */
    
    if (syscall_copy_to_user((void *)info_ptr, &info, sizeof(info)) != 0) {
        return -EFAULT;
    }
    
    return 0;
}

/* System Control (requires privilege) */

static int64_t sys_reboot_impl(uint64_t cmd, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5) {
    (void)cmd; (void)a2; (void)a3; (void)a4; (void)a5;
    
    if (!syscall_has_priv(PRIV_SYSTEM)) {
        return -EPERM;
    }
    
    serial_puts("[SYSCALL] Rebooting system...\n");
    
    /* Triple fault to reboot */
    __asm__ volatile("cli");
    uint64_t null_idt[2] = {0, 0};
    __asm__ volatile("lidt %0" : : "m"(null_idt));
    __asm__ volatile("int $3");
    
    while (1) __asm__ volatile("hlt");
}

static int64_t sys_shutdown_impl(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5) {
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5;
    
    if (!syscall_has_priv(PRIV_SYSTEM)) {
        return -EPERM;
    }
    
    serial_puts("[SYSCALL] Shutting down...\n");
    
    /* ACPI shutdown or halt */
    __asm__ volatile("cli");
    while (1) __asm__ volatile("hlt");
}

/* ============ NXGame Bridge Syscalls ============ */

/* External NXGame Bridge functions */
extern int nxgame_init(void);
extern void *nxgame_session_create(int type, uint32_t caps, void *quotas);
extern int nxgame_session_destroy(void *session);
extern int nxgame_session_start(void *session);
extern int nxgame_session_stop(void *session);
extern int nxgame_gpu_acquire(void *session);
extern void nxgame_gpu_release(void *session);
extern void *nxgame_gpu_get_buffer(void *session);
extern int nxgame_gpu_present(void *session, int wait_vsync);
extern int nxgame_audio_acquire(void *session, uint32_t sample_rate, uint32_t channels, uint32_t buffer_frames);
extern void nxgame_audio_release(void *session);
extern int nxgame_input_acquire(void *session, uint32_t types);
extern void nxgame_input_release(void *session);
extern int nxgame_input_poll(void *session, void *state);

/* Active sessions table (per-process, max 4) */
static void *nxgame_sessions[4] = {NULL, NULL, NULL, NULL};

/* Find free session slot */
static int find_free_session_slot(void) {
    for (int i = 0; i < 4; i++) {
        if (nxgame_sessions[i] == NULL) return i;
    }
    return -1;
}

/* Validate session handle */
static void *get_session(int handle) {
    if (handle < 0 || handle >= 4) return NULL;
    return nxgame_sessions[handle];
}

static int64_t sys_nxgame_create_impl(uint64_t type, uint64_t caps, uint64_t quotas_ptr, uint64_t a4, uint64_t a5) {
    (void)a4; (void)a5;
    
    serial_puts("[SYSCALL] nxgame_create type=");
    serial_dec(type);
    serial_puts("\n");
    
    int slot = find_free_session_slot();
    if (slot < 0) {
        return -EMFILE;  /* Too many sessions */
    }
    
    void *quotas = quotas_ptr ? (void *)quotas_ptr : NULL;
    void *session = nxgame_session_create((int)type, (uint32_t)caps, quotas);
    if (!session) {
        return -ENOMEM;
    }
    
    nxgame_sessions[slot] = session;
    return slot;  /* Return handle */
}

static int64_t sys_nxgame_destroy_impl(uint64_t handle, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5) {
    (void)a2; (void)a3; (void)a4; (void)a5;
    
    void *session = get_session((int)handle);
    if (!session) {
        return -EBADF;
    }
    
    int ret = nxgame_session_destroy(session);
    nxgame_sessions[handle] = NULL;
    return ret;
}

static int64_t sys_nxgame_start_impl(uint64_t handle, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5) {
    (void)a2; (void)a3; (void)a4; (void)a5;
    
    void *session = get_session((int)handle);
    if (!session) {
        return -EBADF;
    }
    
    return nxgame_session_start(session);
}

static int64_t sys_nxgame_stop_impl(uint64_t handle, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5) {
    (void)a2; (void)a3; (void)a4; (void)a5;
    
    void *session = get_session((int)handle);
    if (!session) {
        return -EBADF;
    }
    
    return nxgame_session_stop(session);
}

static int64_t sys_nxgame_gpu_acquire_impl(uint64_t handle, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5) {
    (void)a2; (void)a3; (void)a4; (void)a5;
    
    void *session = get_session((int)handle);
    if (!session) {
        return -EBADF;
    }
    
    return nxgame_gpu_acquire(session);
}

static int64_t sys_nxgame_gpu_release_impl(uint64_t handle, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5) {
    (void)a2; (void)a3; (void)a4; (void)a5;
    
    void *session = get_session((int)handle);
    if (!session) {
        return -EBADF;
    }
    
    nxgame_gpu_release(session);
    return 0;
}

static int64_t sys_nxgame_gpu_present_impl(uint64_t handle, uint64_t wait_vsync, uint64_t a3, uint64_t a4, uint64_t a5) {
    (void)a3; (void)a4; (void)a5;
    
    void *session = get_session((int)handle);
    if (!session) {
        return -EBADF;
    }
    
    return nxgame_gpu_present(session, (int)wait_vsync);
}

static int64_t sys_nxgame_audio_impl(uint64_t handle, uint64_t cmd, uint64_t arg1, uint64_t arg2, uint64_t arg3) {
    void *session = get_session((int)handle);
    if (!session) {
        return -EBADF;
    }
    
    switch (cmd) {
        case 0:  /* Acquire */
            return nxgame_audio_acquire(session, (uint32_t)arg1, (uint32_t)arg2, (uint32_t)arg3);
        case 1:  /* Release */
            nxgame_audio_release(session);
            return 0;
        default:
            return -EINVAL;
    }
}

static int64_t sys_nxgame_input_impl(uint64_t handle, uint64_t state_ptr, uint64_t a3, uint64_t a4, uint64_t a5) {
    (void)a3; (void)a4; (void)a5;
    
    void *session = get_session((int)handle);
    if (!session) {
        return -EBADF;
    }
    
    if (!state_ptr) {
        return -EFAULT;
    }
    
    /* Validate user pointer */
    if (syscall_validate_ptr((void *)state_ptr, 512, 1) != 0) {
        return -EFAULT;
    }
    
    return nxgame_input_poll(session, (void *)state_ptr);
}

static int64_t sys_nxgame_get_buffer_impl(uint64_t handle, uint64_t buf_info_ptr, uint64_t a3, uint64_t a4, uint64_t a5) {
    (void)a3; (void)a4; (void)a5;
    
    void *session = get_session((int)handle);
    if (!session) {
        return -EBADF;
    }
    
    void *buffer = nxgame_gpu_get_buffer(session);
    if (!buffer) {
        return -ENODEV;
    }
    
    /* Copy buffer info to user (first 64 bytes = nxgame_buffer_t) */
    if (syscall_copy_to_user((void *)buf_info_ptr, buffer, 64) != 0) {
        return -EFAULT;
    }
    
    return 0;
}

/* ============ Socket Syscalls ============ */

/* External socket API from TCP/IP stack */
extern int socket_create(int type, int protocol);
extern int socket_bind(int fd, void *addr);
extern int socket_listen(int fd, int backlog);
extern int socket_accept(int fd, void *addr);
extern int socket_connect(int fd, void *addr);
extern int socket_send(int fd, const void *data, uint32_t len);
extern int socket_recv(int fd, void *buf, uint32_t len);
extern int socket_close(int fd);

static int64_t sys_socket_impl(uint64_t type, uint64_t protocol, uint64_t a3, uint64_t a4, uint64_t a5) {
    (void)a3; (void)a4; (void)a5;
    return socket_create((int)type, (int)protocol);
}

static int64_t sys_bind_impl(uint64_t fd, uint64_t addr_ptr, uint64_t a3, uint64_t a4, uint64_t a5) {
    (void)a3; (void)a4; (void)a5;
    if (syscall_validate_ptr((void *)addr_ptr, 16, 0) != 0) {
        return -EFAULT;
    }
    return socket_bind((int)fd, (void *)addr_ptr);
}

static int64_t sys_listen_impl(uint64_t fd, uint64_t backlog, uint64_t a3, uint64_t a4, uint64_t a5) {
    (void)a3; (void)a4; (void)a5;
    return socket_listen((int)fd, (int)backlog);
}

static int64_t sys_accept_impl(uint64_t fd, uint64_t addr_ptr, uint64_t a3, uint64_t a4, uint64_t a5) {
    (void)a3; (void)a4; (void)a5;
    if (addr_ptr && syscall_validate_ptr((void *)addr_ptr, 16, 1) != 0) {
        return -EFAULT;
    }
    return socket_accept((int)fd, (void *)addr_ptr);
}

static int64_t sys_connect_impl(uint64_t fd, uint64_t addr_ptr, uint64_t a3, uint64_t a4, uint64_t a5) {
    (void)a3; (void)a4; (void)a5;
    if (syscall_validate_ptr((void *)addr_ptr, 16, 0) != 0) {
        return -EFAULT;
    }
    return socket_connect((int)fd, (void *)addr_ptr);
}

static int64_t sys_send_impl(uint64_t fd, uint64_t data_ptr, uint64_t len, uint64_t a4, uint64_t a5) {
    (void)a4; (void)a5;
    if (syscall_validate_ptr((void *)data_ptr, len, 0) != 0) {
        return -EFAULT;
    }
    return socket_send((int)fd, (void *)data_ptr, (uint32_t)len);
}

static int64_t sys_recv_impl(uint64_t fd, uint64_t buf_ptr, uint64_t len, uint64_t a4, uint64_t a5) {
    (void)a4; (void)a5;
    if (syscall_validate_ptr((void *)buf_ptr, len, 1) != 0) {
        return -EFAULT;
    }
    return socket_recv((int)fd, (void *)buf_ptr, (uint32_t)len);
}

static int64_t sys_closesock_impl(uint64_t fd, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5) {
    (void)a2; (void)a3; (void)a4; (void)a5;
    return socket_close((int)fd);
}

/* External framebuffer info from video/fbuffer.c or boot info */
extern uint64_t g_framebuffer_addr;
extern uint32_t g_framebuffer_width;
extern uint32_t g_framebuffer_height;
extern uint32_t g_framebuffer_pitch;

/* ============ KERNEL CURSOR COMPOSITOR ============ */
/* Cursor is drawn by kernel AFTER fb_flip - never overwritten by userspace */
/* This mirrors macOS/Windows hardware cursor isolation */

static volatile int32_t g_cursor_x = 100;
static volatile int32_t g_cursor_y = 100;
static volatile int g_cursor_visible = 1;

/* High-contrast cursor bitmap (21x17) - white fill, black outline */
static const uint8_t kernel_cursor_data[21][17] = {
    {1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {1,2,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {1,2,2,1,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {1,2,3,2,1,0,0,0,0,0,0,0,0,0,0,0,0},
    {1,2,3,3,2,1,0,0,0,0,0,0,0,0,0,0,0},
    {1,2,3,3,3,2,1,0,0,0,0,0,0,0,0,0,0},
    {1,2,3,3,3,3,2,1,0,0,0,0,0,0,0,0,0},
    {1,2,3,3,3,3,3,2,1,0,0,0,0,0,0,0,0},
    {1,2,3,3,3,3,3,3,2,1,0,0,0,0,0,0,0},
    {1,2,3,3,3,3,3,3,3,2,1,0,0,0,0,0,0},
    {1,2,3,3,3,3,3,3,3,3,2,1,0,0,0,0,0},
    {1,2,3,3,3,3,3,3,3,3,3,2,1,0,0,0,0},
    {1,2,3,3,3,3,3,2,2,2,2,2,1,0,0,0,0},
    {1,2,3,3,3,2,3,3,2,1,0,0,0,0,0,0,0},
    {1,2,3,3,2,1,2,3,3,2,1,0,0,0,0,0,0},
    {1,2,3,2,1,0,1,2,3,3,2,1,0,0,0,0,0},
    {1,2,2,1,0,0,0,1,2,3,3,2,1,0,0,0,0},
    {1,2,1,0,0,0,0,1,2,3,3,2,1,0,0,0,0},
    {1,1,0,0,0,0,0,0,1,2,3,2,1,0,0,0,0},
    {0,0,0,0,0,0,0,0,1,2,2,1,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0},
};

static void kernel_draw_cursor_on_fb(void) {
    if (!g_cursor_visible || !g_framebuffer_addr) return;
    
    /* Debug: trace cursor drawing periodically */
    static int draw_count = 0;
    draw_count++;
    if (draw_count <= 5 || draw_count % 1000 == 0) {
        serial_puts("[KCURSOR] Drawing at (");
        serial_dec(g_cursor_x);
        serial_puts(", ");
        serial_dec(g_cursor_y);
        serial_puts(") #");
        serial_dec(draw_count);
        serial_puts("\n");
    }
    
    uint32_t *fb = (uint32_t *)g_framebuffer_addr;
    uint32_t pitch_px = g_framebuffer_pitch / 4;
    
    for (int row = 0; row < 21; row++) {
        int py = g_cursor_y + row;
        if (py < 0 || py >= (int32_t)g_framebuffer_height) continue;
        
        for (int col = 0; col < 17; col++) {
            int px = g_cursor_x + col;
            if (px < 0 || px >= (int32_t)g_framebuffer_width) continue;
            
            uint8_t pix = kernel_cursor_data[row][col];
            uint32_t color = 0;
            switch (pix) {
                case 1:
                case 2: color = 0xFF000000; break;  /* BLACK outline */
                case 3: color = 0xFFFFFFFF; break;  /* WHITE fill */
                default: continue;
            }
            fb[py * pitch_px + px] = color;
        }
    }
}

/* Syscall: Set cursor position from userspace */
static int64_t sys_cursor_set_impl(uint64_t x, uint64_t y, uint64_t visible, uint64_t a4, uint64_t a5) {
    (void)a4; (void)a5;
    g_cursor_x = (int32_t)x;
    g_cursor_y = (int32_t)y;
    g_cursor_visible = (int)visible;
    return 0;
}
/* ============ END KERNEL CURSOR COMPOSITOR ============ */

static int64_t sys_fb_info_impl(uint64_t info_ptr, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5) {
    (void)a2; (void)a3; (void)a4; (void)a5;
    
    if (syscall_validate_ptr((void *)info_ptr, sizeof(fb_info_t), 1) != 0) {
        return -EFAULT;
    }
    
    fb_info_t info;
    info.base_addr = g_framebuffer_addr;
    info.width = g_framebuffer_width;
    info.height = g_framebuffer_height;
    info.pitch = g_framebuffer_pitch;
    info.bpp = 32;
    info.format = 0;  /* BGRA */
    info.size = (uint64_t)g_framebuffer_pitch * g_framebuffer_height;
    
    if (syscall_copy_to_user((void *)info_ptr, &info, sizeof(fb_info_t)) != 0) {
        return -EFAULT;
    }
    
    serial_puts("[SYSCALL] fb_info: ");
    serial_dec(info.width);
    serial_puts("x");
    serial_dec(info.height);
    serial_puts("\n");
    
    return 0;
}

static int64_t sys_fb_map_impl(uint64_t info_ptr, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5) {
    (void)a2; (void)a3; (void)a4; (void)a5;
    
    /* For now, just return the framebuffer address (already identity mapped) */
    /* TODO: Properly map into user address space with mmap */
    
    if (info_ptr) {
        if (syscall_validate_ptr((void *)info_ptr, sizeof(fb_info_t), 1) != 0) {
            return -EFAULT;
        }
        
        fb_info_t info;
        info.base_addr = g_framebuffer_addr;
        info.width = g_framebuffer_width;
        info.height = g_framebuffer_height;
        info.pitch = g_framebuffer_pitch;
        info.bpp = 32;
        info.format = 0;
        info.size = (uint64_t)g_framebuffer_pitch * g_framebuffer_height;
        
        if (syscall_copy_to_user((void *)info_ptr, &info, sizeof(fb_info_t)) != 0) {
            return -EFAULT;
        }
    }
    
    serial_puts("[SYSCALL] fb_map: returning addr 0x");
    /* Print hex address */
    uint64_t addr = g_framebuffer_addr;
    char hex[17];
    for (int i = 15; i >= 0; i--) {
        int d = addr & 0xF;
        hex[i] = d < 10 ? '0' + d : 'A' + d - 10;
        addr >>= 4;
    }
    hex[16] = '\0';
    serial_puts(hex);
    serial_puts("\n");
    
    return (int64_t)g_framebuffer_addr;
}

static int64_t sys_fb_flip_impl(uint64_t src_ptr, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5) {
    (void)a2; (void)a3; (void)a4; (void)a5;
    
    /* Debug trace */
    static int flip_count = 0;
    flip_count++;
    if (flip_count <= 5 || flip_count % 500 == 0) {
        serial_puts("[FB_FLIP] Called #");
        serial_dec(flip_count);
        serial_puts(" src=");
        serial_dec((int64_t)src_ptr);
        serial_puts("\n");
    }
    
    /* If userspace provides a back buffer, copy it to front buffer */
    if (src_ptr && g_framebuffer_addr) {
        uint64_t fb_size = (uint64_t)g_framebuffer_pitch * g_framebuffer_height;
        
        /* NOTE: VGA VSync (port 0x3DA) does NOT work with UEFI GOP! */
        /* UEFI GOP has no VSync support - just do fast copy */
        
        /* Fast copy using REP MOVSQ (8 bytes at a time) */
        uint64_t *src = (uint64_t *)src_ptr;
        uint64_t *dst = (uint64_t *)g_framebuffer_addr;
        uint64_t count = fb_size / 8;
        
        __asm__ volatile(
            "rep movsq"
            : "+S"(src), "+D"(dst), "+c"(count)
            :
            : "memory"
        );
    }
    
    /* ========== KERNEL CURSOR COMPOSITOR ========== */
    /* Draw cursor AFTER fb copy (or in single-buffer mode) */
    /* This is how macOS/Windows achieve flicker-free cursors */
    /* ALWAYS draw cursor, even if no buffer copy happened */
    kernel_draw_cursor_on_fb();
    /* ============================================== */
    
    return 0;
}

/* ============ Input Syscalls (for userspace desktop) ============ */

/* External input functions from keyboard/mouse drivers */
extern int keyboard_get_event(uint8_t *scancode, uint8_t *ascii, uint8_t *pressed);
extern int mouse_get_event(int16_t *dx, int16_t *dy, uint8_t *buttons);

static int64_t sys_input_poll_impl(uint64_t events_ptr, uint64_t max_events, uint64_t a3, uint64_t a4, uint64_t a5) {
    (void)a3; (void)a4; (void)a5;
    
    if (max_events == 0) return 0;
    
    if (syscall_validate_ptr((void *)events_ptr, max_events * sizeof(input_event_t), 1) != 0) {
        return -EFAULT;
    }
    
    input_event_t *events = (input_event_t *)events_ptr;
    int count = 0;
    
    /* Poll keyboard events */
    uint8_t scancode, ascii, pressed;
    while (count < (int)max_events && keyboard_get_event(&scancode, &ascii, &pressed) == 0) {
        events[count].type = INPUT_TYPE_KEYBOARD;
        events[count].timestamp = (uint32_t)pit_get_ticks();
        events[count].keyboard.scancode = scancode;
        events[count].keyboard.ascii = ascii;
        events[count].keyboard.pressed = pressed;
        events[count].keyboard.modifiers = 0;  /* TODO: Get modifier state */
        count++;
    }
    
    /* Debug trace: periodically log mouse status */
    static uint32_t poll_trace_counter = 0;
    poll_trace_counter++;
    if (poll_trace_counter % 5000 == 1) {
        extern void mouse_debug_status(void);
        mouse_debug_status();
    }
    
    /* Poll mouse events - EMIT UNCONDITIONALLY (never drop!) */
    /* This is critical: Apple/Linux always emit events from hardware */
    /* Userspace decides what "movement" means, not the kernel */
    int16_t dx, dy;
    uint8_t buttons;
    while (count < (int)max_events && mouse_get_event(&dx, &dy, &buttons) == 0) {
        /* Always emit mouse event - even if dx=dy=0 */
        events[count].type = INPUT_TYPE_MOUSE_MOVE;
        events[count].timestamp = (uint32_t)pit_get_ticks();
        events[count].mouse.dx = dx;
        events[count].mouse.dy = dy;
        events[count].mouse.buttons = buttons;
        count++;
    }
    
    return count;
}

static int64_t sys_input_register_impl(uint64_t types, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5) {
    (void)types; (void)a2; (void)a3; (void)a4; (void)a5;
    
    /* TODO: Register process for specific input types */
    /* For now, all processes can receive all input */
    
    serial_puts("[SYSCALL] input_register: types=");
    serial_dec(types);
    serial_puts("\n");
    
    return 0;
}

/* ============ Debug Syscalls ============ */

static int64_t sys_debug_print_impl(uint64_t str_ptr, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5) {
    (void)a2; (void)a3; (void)a4; (void)a5;
    
    if (syscall_validate_ptr((void *)str_ptr, 1, 0) != 0) {
        return -EFAULT;
    }
    
    /* Print userspace string to serial (limit to 256 chars for safety) */
    const char *s = (const char *)str_ptr;
    for (int i = 0; i < 256 && s[i]; i++) {
        serial_putc(s[i]);
    }
    
    return 0;
}

/* ============ RTC Syscalls (Real-Time Clock) ============ */

/* External RTC functions */
extern void rtc_read_time(void *time);
extern void rtc_write_time(const void *time);
extern uint64_t rtc_get_unix_time(void);

/* RTC time structure (must match drivers/rtc.h) */
typedef struct {
    uint16_t year;
    uint8_t  month;
    uint8_t  day;
    uint8_t  hour;
    uint8_t  minute;
    uint8_t  second;
    uint8_t  weekday;
} __attribute__((packed)) rtc_time_syscall_t;

static int64_t sys_rtc_get_impl(uint64_t time_ptr, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5) {
    (void)a2; (void)a3; (void)a4; (void)a5;
    
    if (!time_ptr) return -EINVAL;
    
    if (syscall_validate_ptr((void *)time_ptr, sizeof(rtc_time_syscall_t), 1) != 0) {
        return -EFAULT;
    }
    
    rtc_time_syscall_t time;
    rtc_read_time(&time);
    
    if (syscall_copy_to_user((void *)time_ptr, &time, sizeof(time)) != 0) {
        return -EFAULT;
    }
    
    return 0;
}

static int64_t sys_rtc_set_impl(uint64_t time_ptr, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5) {
    (void)a2; (void)a3; (void)a4; (void)a5;
    
    /* Requires system privilege */
    if (!syscall_has_priv(PRIV_SYSTEM)) {
        return -EPERM;
    }
    
    if (!time_ptr) return -EINVAL;
    
    if (syscall_validate_ptr((void *)time_ptr, sizeof(rtc_time_syscall_t), 0) != 0) {
        return -EFAULT;
    }
    
    rtc_time_syscall_t time;
    if (syscall_copy_from_user(&time, (void *)time_ptr, sizeof(time)) != 0) {
        return -EFAULT;
    }
    
    rtc_write_time(&time);
    return 0;
}

static int64_t sys_rtc_unix_impl(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5) {
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5;
    return (int64_t)rtc_get_unix_time();
}

/* ============ Syscall Dispatch ============ */

int64_t syscall_handler(uint64_t num, uint64_t arg1, uint64_t arg2,
                        uint64_t arg3, uint64_t arg4, uint64_t arg5) {
    /* Trace if enabled */
    if (SYSCALL_TRACE_ENABLED) {
        serial_puts("[SYSCALL] ");
        serial_dec(num);
        serial_puts("(");
        serial_dec(arg1);
        serial_puts(")\n");
    }
    
    /* Validate syscall number */
    if (num >= SYS_MAX || syscall_table[num] == NULL) {
        serial_puts("[SYSCALL] Unknown syscall: ");
        serial_dec(num);
        serial_puts(" (0x");
        /* Print hex for debugging */
        uint64_t tmp = num;
        char hex[17];
        for (int i = 15; i >= 0; i--) {
            int d = tmp & 0xF;
            hex[i] = d < 10 ? '0' + d : 'A' + d - 10;
            tmp >>= 4;
        }
        hex[16] = '\0';
        serial_puts(hex);
        serial_puts(") arg1=");
        serial_dec((int64_t)arg1);
        serial_puts("\n");
        return -ENOSYS;
    }
    
    /* Check privilege */
    if (syscall_priv[num] > PRIV_USER && !syscall_has_priv(syscall_priv[num])) {
        return -EPERM;
    }
    
    /* Dispatch */
    int64_t ret = syscall_table[num](arg1, arg2, arg3, arg4, arg5);
    
    if (SYSCALL_TRACE_ENABLED) {
        serial_puts("[SYSCALL] -> ");
        serial_dec(ret);
        serial_puts("\n");
    }
    
    return ret;
}

/* ============ Initialization ============ */

void syscall_init(void) {
    serial_puts("[SYSCALL] Initializing syscall subsystem...\n");
    
    /* Clear tables */
    for (int i = 0; i < SYS_MAX; i++) {
        syscall_table[i] = NULL;
        syscall_priv[i] = PRIV_USER;
    }
    
    /* Register syscalls */
    syscall_table[SYS_EXIT] = sys_exit_impl;
    syscall_table[SYS_SLEEP] = sys_sleep_impl;
    syscall_table[SYS_TIME] = sys_time_impl;
    syscall_table[SYS_YIELD] = sys_yield_impl;
    syscall_table[SYS_GETPID] = sys_getpid_impl;
    syscall_table[SYS_GETPPID] = sys_getppid_impl;
    syscall_table[SYS_FORK] = sys_fork_impl;
    syscall_table[SYS_EXEC] = sys_exec_impl;
    syscall_table[SYS_WAIT] = sys_wait_impl;
    
    syscall_table[SYS_BRK] = sys_brk_impl;
    
    syscall_table[SYS_OPEN] = sys_open_impl;
    syscall_table[SYS_CLOSE] = sys_close_impl;
    syscall_table[SYS_READ] = sys_read_impl;
    syscall_table[SYS_WRITE] = sys_write_impl;
    syscall_table[SYS_SEEK] = sys_seek_impl;
    syscall_table[SYS_STAT] = sys_stat_impl;
    
    /* Directory operations */
    syscall_table[SYS_READDIR] = sys_readdir_impl;
    syscall_table[SYS_MKDIR] = sys_mkdir_impl;
    syscall_table[SYS_RMDIR] = sys_unlink_impl;
    syscall_table[SYS_CHDIR] = sys_chdir_impl;
    syscall_table[SYS_GETCWD] = sys_getcwd_impl;
    
    syscall_table[SYS_INFO] = sys_info_impl;
    
    syscall_table[SYS_REBOOT] = sys_reboot_impl;
    syscall_priv[SYS_REBOOT] = PRIV_SYSTEM;
    
    syscall_table[SYS_SHUTDOWN] = sys_shutdown_impl;
    syscall_priv[SYS_SHUTDOWN] = PRIV_SYSTEM;
    
    /* NXGame Bridge syscalls */
    syscall_table[SYS_NXGAME_CREATE] = sys_nxgame_create_impl;
    syscall_table[SYS_NXGAME_DESTROY] = sys_nxgame_destroy_impl;
    syscall_table[SYS_NXGAME_START] = sys_nxgame_start_impl;
    syscall_table[SYS_NXGAME_STOP] = sys_nxgame_stop_impl;
    syscall_table[SYS_NXGAME_GPU_ACQUIRE] = sys_nxgame_gpu_acquire_impl;
    syscall_table[SYS_NXGAME_GPU_RELEASE] = sys_nxgame_gpu_release_impl;
    syscall_table[SYS_NXGAME_GPU_PRESENT] = sys_nxgame_gpu_present_impl;
    syscall_table[SYS_NXGAME_AUDIO] = sys_nxgame_audio_impl;
    syscall_table[SYS_NXGAME_INPUT] = sys_nxgame_input_impl;
    syscall_table[SYS_NXGAME_GET_BUFFER] = sys_nxgame_get_buffer_impl;
    
    /* Socket syscalls */
    syscall_table[SYS_SOCKET] = sys_socket_impl;
    syscall_table[SYS_BIND] = sys_bind_impl;
    syscall_table[SYS_LISTEN] = sys_listen_impl;
    syscall_table[SYS_ACCEPT] = sys_accept_impl;
    syscall_table[SYS_CONNECT] = sys_connect_impl;
    syscall_table[SYS_SEND] = sys_send_impl;
    syscall_table[SYS_RECV] = sys_recv_impl;
    syscall_table[SYS_CLOSESOCK] = sys_closesock_impl;
    
    /* Framebuffer syscalls (for userspace desktop) */
    syscall_table[SYS_FB_MAP] = sys_fb_map_impl;
    syscall_table[SYS_FB_INFO] = sys_fb_info_impl;
    syscall_table[SYS_FB_FLIP] = sys_fb_flip_impl;
    
    /* Input syscalls (for userspace desktop) */
    syscall_table[SYS_INPUT_POLL] = sys_input_poll_impl;
    syscall_table[SYS_INPUT_REGISTER] = sys_input_register_impl;
    
    /* Debug syscalls */
    syscall_table[SYS_DEBUG_PRINT] = sys_debug_print_impl;
    
    /* Cursor syscalls (kernel cursor compositor) */
    syscall_table[SYS_CURSOR_SET] = sys_cursor_set_impl;
    
    /* RTC syscalls (real-time clock) */
    syscall_table[SYS_RTC_GET] = sys_rtc_get_impl;
    syscall_table[SYS_RTC_SET] = sys_rtc_set_impl;
    syscall_priv[SYS_RTC_SET] = PRIV_SYSTEM;  /* Setting clock requires privilege */
    syscall_table[SYS_RTC_UNIX] = sys_rtc_unix_impl;
    
    /* Set up MSRs */
    syscall_msr_init();
    syscall_enable();
    
    serial_puts("[SYSCALL] Ready (");
    serial_dec(49);  /* 23 base + 10 NXGame + 8 Socket + 5 FB/Input + 3 RTC */
    serial_puts(" syscalls registered)\n");
}

void syscall_trace(uint64_t num, uint64_t ret) {
    serial_puts("[TRACE] syscall ");
    serial_dec(num);
    serial_puts(" = ");
    serial_dec(ret);
    serial_puts("\n");
}