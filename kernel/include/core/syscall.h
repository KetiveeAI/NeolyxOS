/*
 * NeolyxOS System Call Interface
 * 
 * STABLE ABI - DO NOT BREAK ONCE PUBLISHED
 * 
 * Mechanism: SYSCALL/SYSRET (x86_64)
 * 
 * Register Convention:
 *   RAX  → syscall number
 *   RDI  → arg1
 *   RSI  → arg2
 *   RDX  → arg3
 *   R10  → arg4 (RCX clobbered by SYSCALL)
 *   R8   → arg5
 *   R9   → arg6
 *   RAX  ← return value
 * 
 * Error Convention:
 *   RAX < 0 → -errno
 * 
 * Copyright (c) 2025 KetiveeAI. All rights reserved.
 * See LICENSE.md for terms.
 */

#ifndef NEOLYX_SYSCALL_H
#define NEOLYX_SYSCALL_H

#include <stdint.h>
#include <stddef.h>

/* ============ Syscall Numbers ============ */
/* STABLE: Once published, these numbers NEVER change */

/* Process Control */
#define SYS_EXIT        1   /* void sys_exit(int code) */
#define SYS_SLEEP       2   /* int sys_sleep(uint32_t ms) */
#define SYS_TIME        3   /* uint64_t sys_time(void) */
#define SYS_YIELD       4   /* void sys_yield(void) */
#define SYS_GETPID      5   /* int32_t sys_getpid(void) */
#define SYS_GETPPID     6   /* int32_t sys_getppid(void) */
#define SYS_FORK        7   /* int32_t sys_fork(void) */
#define SYS_EXEC        8   /* int sys_exec(const char* path, char** argv, char** envp) */
#define SYS_WAIT        9   /* int32_t sys_wait(int32_t pid, int* status) */

/* Memory Management */
#define SYS_BRK         10  /* void* sys_brk(void* addr) */
#define SYS_MMAP        11  /* void* sys_mmap(void* addr, size_t len, int prot, int flags) */
#define SYS_MUNMAP      12  /* int sys_munmap(void* addr, size_t len) */

/* File I/O (VFS-backed) */
#define SYS_OPEN        20  /* int sys_open(const char* path, int flags, int mode) */
#define SYS_CLOSE       21  /* int sys_close(int fd) */
#define SYS_READ        22  /* ssize_t sys_read(int fd, void* buf, size_t count) */
#define SYS_WRITE       23  /* ssize_t sys_write(int fd, const void* buf, size_t count) */
#define SYS_SEEK        24  /* off_t sys_seek(int fd, off_t offset, int whence) */
#define SYS_STAT        25  /* int sys_stat(const char* path, struct stat* st) */
#define SYS_FSTAT       26  /* int sys_fstat(int fd, struct stat* st) */

/* Directory Operations */
#define SYS_READDIR     30  /* int sys_readdir(int fd, struct dirent* entry) */
#define SYS_MKDIR       31  /* int sys_mkdir(const char* path, int mode) */
#define SYS_RMDIR       32  /* int sys_rmdir(const char* path) */
#define SYS_CHDIR       33  /* int sys_chdir(const char* path) */
#define SYS_GETCWD      34  /* int sys_getcwd(char* buf, size_t size) */

/* Networking / Sockets */
#define SYS_SOCKET      40  /* int sys_socket(int type, int protocol) */
#define SYS_BIND        41  /* int sys_bind(int fd, sockaddr_in_t* addr) */
#define SYS_LISTEN      42  /* int sys_listen(int fd, int backlog) */
#define SYS_ACCEPT      43  /* int sys_accept(int fd, sockaddr_in_t* addr) */
#define SYS_CONNECT     44  /* int sys_connect(int fd, sockaddr_in_t* addr) */
#define SYS_SEND        45  /* ssize_t sys_send(int fd, void* buf, size_t len) */
#define SYS_RECV        46  /* ssize_t sys_recv(int fd, void* buf, size_t len) */
#define SYS_CLOSESOCK   47  /* int sys_closesock(int fd) */

/* System Information */
#define SYS_INFO        50  /* int sys_info(struct sysinfo* info) */
#define SYS_UNAME       51  /* int sys_uname(struct utsname* buf) */

/* Pseudo-Terminal (PTY) for Terminal.app */
#define SYS_PTY_OPEN    60  /* int sys_pty_open(int* master_fd, int* slave_fd) */
#define SYS_PTY_READ    61  /* ssize_t sys_pty_read(int fd, void* buf, size_t count) */
#define SYS_PTY_WRITE   62  /* ssize_t sys_pty_write(int fd, const void* buf, size_t count) */
#define SYS_PTY_CLOSE   63  /* int sys_pty_close(int fd) */
#define SYS_PTY_RESIZE  64  /* int sys_pty_resize(int fd, int cols, int rows) */
#define SYS_PTY_IOCTL   65  /* int sys_pty_ioctl(int fd, int cmd, void* arg) */
#define SYS_SPAWN_SHELL 66  /* int sys_spawn_shell(int pty_slave_fd, const char* shell) */
#define SYS_PTY_POLL    67  /* int sys_pty_poll(int fd, int timeout_ms) */

/* System Control (requires SYS privilege) */
#define SYS_REBOOT      100 /* int sys_reboot(int cmd) - SYS ONLY */
#define SYS_SHUTDOWN    101 /* int sys_shutdown(void) - SYS ONLY */
#define SYS_MOUNT       102 /* int sys_mount(...) - SYS ONLY */
#define SYS_UMOUNT      103 /* int sys_umount(const char* path) - SYS ONLY */

#define SYS_NXGAME_CREATE       110 /* Create NXGame session */
#define SYS_NXGAME_DESTROY      111 /* Destroy NXGame session */
#define SYS_NXGAME_START        112 /* Start exclusive mode */
#define SYS_NXGAME_STOP         113 /* Stop exclusive mode */
#define SYS_NXGAME_GPU_ACQUIRE  114 /* Acquire exclusive GPU */
#define SYS_NXGAME_GPU_RELEASE  115 /* Release GPU */
#define SYS_NXGAME_GPU_PRESENT  116 /* Present frame (flip) */
#define SYS_NXGAME_AUDIO        117 /* Audio control */
#define SYS_NXGAME_INPUT        118 /* Poll raw input */
#define SYS_NXGAME_GET_BUFFER   119 /* Get framebuffer info */

/* ============ Framebuffer Syscalls (for userspace desktop) ============ */
/* These enable proper kernel/userspace separation for the desktop shell */
#define SYS_FB_MAP              120 /* void* sys_fb_map(fb_info_t* info) - Map framebuffer */
#define SYS_FB_INFO             121 /* int sys_fb_info(fb_info_t* info) - Get framebuffer info */
#define SYS_FB_FLIP             122 /* int sys_fb_flip(void) - Swap buffers (VSync) */

/* ============ Input Syscalls (for userspace desktop) ============ */
#define SYS_INPUT_POLL          123 /* int sys_input_poll(input_event_t* events, int max) */
#define SYS_INPUT_REGISTER      124 /* int sys_input_register(uint32_t types) */

/* ============ Debug Syscalls ============ */
#define SYS_DEBUG_PRINT         127 /* void sys_debug_print(const char* msg) - Print to serial */

/* ============ Cursor Syscalls (kernel cursor compositor) ============ */
#define SYS_CURSOR_SET          128 /* int sys_cursor_set(int x, int y, int visible) */

/* ============ RTC Syscalls (Real-Time Clock) ============ */
#define SYS_RTC_GET             130 /* int sys_rtc_get(rtc_time_t* time) - Get real time */
#define SYS_RTC_SET             131 /* int sys_rtc_set(rtc_time_t* time) - Set real time */
#define SYS_RTC_UNIX            132 /* uint64_t sys_rtc_unix(void) - Get Unix timestamp */

/* Maximum syscall number */
#define SYS_MAX         150

/* ============ Error Codes ============ */
/* POSIX-compatible */

#define EPERM           1   /* Operation not permitted */
#define ENOENT          2   /* No such file or directory */
#define ESRCH           3   /* No such process */
#define EINTR           4   /* Interrupted syscall */
#define EIO             5   /* I/O error */
#define ENXIO           6   /* No such device */
#define E2BIG           7   /* Argument list too long */
#define ENOEXEC         8   /* Exec format error */
#define EBADF           9   /* Bad file descriptor */
#define ECHILD          10  /* No child processes */
#define EAGAIN          11  /* Try again */
#define ENOMEM          12  /* Out of memory */
#define EACCES          13  /* Permission denied */
#define EFAULT          14  /* Bad address */
#define EBUSY           16  /* Device busy */
#define EEXIST          17  /* File exists */
#define EXDEV           18  /* Cross-device link */
#define ENODEV          19  /* No such device */
#define ENOTDIR         20  /* Not a directory */
#define EISDIR          21  /* Is a directory */
#define EINVAL          22  /* Invalid argument */
#define ENFILE          23  /* Too many open files (system) */
#define EMFILE          24  /* Too many open files (process) */
#define ENOTTY          25  /* Not a tty */
#define EFBIG           27  /* File too large */
#define ENOSPC          28  /* No space left */
#define ESPIPE          29  /* Illegal seek */
#define EROFS           30  /* Read-only filesystem */
#define EPIPE           32  /* Broken pipe */
#define ENOSYS          38  /* Function not implemented */
#define ENOTEMPTY       39  /* Directory not empty */
#define ERANGE          40  /* Math result not representable */

/* ============ Privilege Levels ============ */

#define PRIV_USER       0   /* Normal user syscalls */
#define PRIV_SYSTEM     1   /* Elevated syscalls (sys command) */

/* ============ Structures ============ */

/* System info structure */
typedef struct {
    char os_name[32];       /* "NeolyxOS" */
    char os_version[32];    /* "1.0.0" */
    char arch[16];          /* "x86_64" */
    uint64_t uptime_ms;
    uint64_t total_memory;
    uint64_t free_memory;
    uint32_t num_cpus;
    uint32_t num_processes;
} sysinfo_t;

/* File stat structure */
typedef struct {
    uint64_t st_ino;
    uint32_t st_mode;
    uint32_t st_nlink;
    uint32_t st_uid;
    uint32_t st_gid;
    uint64_t st_size;
    uint64_t st_atime;
    uint64_t st_mtime;
    uint64_t st_ctime;
} stat_t;

/* utsname structure */
typedef struct {
    char sysname[65];
    char nodename[65];
    char release[65];
    char version[65];
    char machine[65];
} utsname_t;

/* Framebuffer info structure (for userspace desktop) */
typedef struct {
    uint64_t base_addr;     /* Physical/mapped address of framebuffer */
    uint32_t width;         /* Width in pixels */
    uint32_t height;        /* Height in pixels */
    uint32_t pitch;         /* Bytes per row */
    uint32_t bpp;           /* Bits per pixel (usually 32) */
    uint32_t format;        /* Pixel format (0=BGRA, 1=RGBA) */
    uint64_t size;          /* Total framebuffer size in bytes */
} fb_info_t;

/* Input event structure (for userspace desktop) */
typedef struct {
    uint32_t type;          /* 0=keyboard, 1=mouse_move, 2=mouse_button */
    uint32_t timestamp;     /* Milliseconds since boot */
    union {
        struct {
            uint8_t scancode;
            uint8_t ascii;
            uint8_t pressed;    /* 1=pressed, 0=released */
            uint8_t modifiers;  /* shift, ctrl, alt flags */
        } keyboard;
        struct {
            int16_t dx;         /* Delta X movement */
            int16_t dy;         /* Delta Y movement */
            uint8_t buttons;    /* Button state bitmap */
            uint8_t _pad[3];
        } mouse;
    };
} input_event_t;

/* Input event types */
#define INPUT_TYPE_KEYBOARD     0
#define INPUT_TYPE_MOUSE_MOVE   1
#define INPUT_TYPE_MOUSE_BUTTON 2

/* ============ Capability Check ============ */

/**
 * Check if current process has system privilege.
 */
int syscall_has_priv(int level);

/**
 * Elevate to system privilege (temporary, for single syscall).
 */
int syscall_elevate(void);

/* ============ User Memory Validation ============ */

/**
 * Validate user pointer (check if accessible).
 * @return 0 if valid, -EFAULT if invalid
 */
int syscall_validate_ptr(const void *ptr, size_t size, int write);

/**
 * Copy from user space to kernel.
 * @return 0 on success, -EFAULT on error
 */
int syscall_copy_from_user(void *dst, const void *src, size_t size);

/**
 * Copy from kernel to user space.
 * @return 0 on success, -EFAULT on error
 */
int syscall_copy_to_user(void *dst, const void *src, size_t size);

/**
 * Copy string from user space.
 * @return string length, or -EFAULT
 */
int syscall_strncpy_from_user(char *dst, const char *src, size_t max);

/* ============ Syscall Dispatch ============ */

/**
 * Initialize syscall subsystem (MSRs for SYSCALL/SYSRET).
 */
void syscall_init(void);

/**
 * Main syscall handler (called from assembly stub).
 */
int64_t syscall_handler(uint64_t num, uint64_t arg1, uint64_t arg2, 
                        uint64_t arg3, uint64_t arg4, uint64_t arg5);

/* ============ Debug ============ */

#ifdef SYSCALL_TRACE
#define SYSCALL_TRACE_ENABLED 1
#else
#define SYSCALL_TRACE_ENABLED 0
#endif

/**
 * Log syscall for debugging.
 */
void syscall_trace(uint64_t num, uint64_t ret);

#endif /* NEOLYX_SYSCALL_H */
