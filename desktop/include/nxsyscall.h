/*
 * NeolyxOS Userspace System Call Interface
 * 
 * Provides userspace wrappers for kernel syscalls.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef NXSYSCALL_H
#define NXSYSCALL_H

#include <stdint.h>

/* ============ Syscall Numbers (MUST MATCH kernel/include/core/syscall.h) ============ */

/* Process Control */
#define SYS_EXIT        1
#define SYS_SLEEP       2   /* Kernel: line 36 */
#define SYS_TIME        3   /* Kernel: line 37 (use SYS_TIME, not SYS_GETTIME) */
#define SYS_YIELD       4
#define SYS_GETPID      5

/* Process creation */
#define SYS_SPAWN       13
#define SPAWN_WAITABLE  0x1     /* Keep child for wait(); default: detached */

/* Directory listing */
#define SYS_READDIR     30

/* Memory Management */
#define SYS_BRK         10
#define SYS_MMAP        11
#define SYS_MUNMAP      12

/* File I/O (VFS-backed) */
#define SYS_OPEN        20
#define SYS_CLOSE       21
#define SYS_READ        22
#define SYS_WRITE       23
#define SYS_STAT        25
#define SYS_SEEK        24
#define SYS_MKDIR       31
#define SYS_REMOVE      32  /* kernel name "rmdir": deletes a file or an empty directory */
#define SYS_RENAME      35

/* File open flags - MUST MATCH the kernel's VFS_O_* (include/fs/vfs.h) */
#define O_RDONLY  0x0001
#define O_WRONLY  0x0002
#define O_RDWR    0x0003
#define O_CREAT   0x0100
#define O_TRUNC   0x0200
#define O_APPEND  0x0400

/* Seek origins */
#define SEEK_SET  0
#define SEEK_CUR  1
#define SEEK_END  2

/* Framebuffer (120-122) */
#define SYS_FB_MAP      120
#define SYS_FB_INFO     121
#define SYS_FB_FLIP     122

/* Input (123-124) */
#define SYS_INPUT_POLL      123
#define SYS_INPUT_REGISTER  124

/* ============ Data Structures ============ */

/* Framebuffer info structure - MUST MATCH KERNEL'S fb_info_t EXACTLY! */
typedef struct {
    uint64_t base_addr;     /* Physical/mapped address of framebuffer */
    uint32_t width;         /* Width in pixels */
    uint32_t height;        /* Height in pixels */
    uint32_t pitch;         /* Bytes per row */
    uint32_t bpp;           /* Bits per pixel (usually 32) */
    uint32_t format;        /* Pixel format (0=BGRA, 1=RGBA) */
    uint64_t size;          /* Total framebuffer size in bytes */
} fb_info_t;

/* Input event structure - MUST MATCH KERNEL'S input_event_t EXACTLY! */
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
            int8_t  wheel;      /* Vertical wheel notches since the last
                                 * event; > 0 = toward the user (scroll
                                 * down). Was padding: older readers see 0. */
            uint8_t _pad[2];
        } mouse;
    };
} input_event_t;

/* Input event types */
#define INPUT_TYPE_KEYBOARD     0
#define INPUT_TYPE_MOUSE_MOVE   1
#define INPUT_TYPE_MOUSE_BUTTON 2

/* ============ Syscall Macros ============ */

#ifdef NX_HOST_PREVIEW
/*
 * Host preview build (desktop/tools/preview): the same wrappers route to a
 * dispatcher implemented by the harness instead of the SYSCALL instruction,
 * so shell code renders unmodified on the development machine.
 */
int64_t nx_host_syscall(int64_t num, int64_t a1, int64_t a2, int64_t a3);
static inline int64_t syscall0(int64_t num) { return nx_host_syscall(num, 0, 0, 0); }
static inline int64_t syscall1(int64_t num, int64_t a1) { return nx_host_syscall(num, a1, 0, 0); }
static inline int64_t syscall2(int64_t num, int64_t a1, int64_t a2) {
    return nx_host_syscall(num, a1, a2, 0);
}
static inline int64_t syscall3(int64_t num, int64_t a1, int64_t a2, int64_t a3) {
    return nx_host_syscall(num, a1, a2, a3);
}
int64_t nx_host_syscall4(int64_t num, int64_t a1, int64_t a2, int64_t a3, int64_t a4);
static inline int64_t syscall4(int64_t num, int64_t a1, int64_t a2, int64_t a3, int64_t a4) {
    return nx_host_syscall4(num, a1, a2, a3, a4);
}
#else
/* Inline syscall with 0-3 arguments */
static inline int64_t syscall0(int64_t num) {
    int64_t ret;
    __asm__ volatile(
        "syscall"
        : "=a"(ret)
        : "a"(num)
        : "rcx", "r11", "memory"
    );
    return ret;
}

static inline int64_t syscall1(int64_t num, int64_t a1) {
    int64_t ret;
    __asm__ volatile(
        "syscall"
        : "=a"(ret)
        : "0"(num), "D"(a1)   /* "0" ties num to same register as output operand 0 (RAX) */
        : "rcx", "r11", "memory"
    );
    return ret;
}

static inline int64_t syscall2(int64_t num, int64_t a1, int64_t a2) {
    int64_t ret;
    __asm__ volatile(
        "syscall"
        : "=a"(ret)
        : "a"(num), "D"(a1), "S"(a2)
        : "rcx", "r11", "memory"
    );
    return ret;
}

/* Kernel ABI: RDI, RSI, RDX, R10, R8 carry arguments 1-5; only RAX
 * (result), RCX and R11 are clobbered. */
static inline int64_t syscall3(int64_t num, int64_t a1, int64_t a2, int64_t a3) {
    int64_t ret;
    __asm__ volatile(
        "syscall"
        : "=a"(ret)
        : "a"(num), "D"(a1), "S"(a2), "d"(a3)
        : "rcx", "r11", "memory"
    );
    return ret;
}
static inline int64_t syscall4(int64_t num, int64_t a1, int64_t a2, int64_t a3, int64_t a4) {
    int64_t ret;
    register int64_t r10 __asm__("r10") = a4;
    __asm__ volatile(
        "syscall"
        : "=a"(ret)
        : "a"(num), "D"(a1), "S"(a2), "d"(a3), "r"(r10)
        : "rcx", "r11", "memory"
    );
    return ret;
}
#endif /* NX_HOST_PREVIEW */

/* ============ Process Wrappers ============ */

/*
 * Start a program from disk as a new process (own address space) that
 * runs alongside the caller. argv may be NULL (argv[0] = path).
 * Returns the child's PID, or a negative error code.
 */
static inline int nx_spawn(const char *path, char *const argv[], uint32_t flags) {
    return (int)syscall3(SYS_SPAWN, (int64_t)path, (int64_t)argv, (int64_t)flags);
}

/* ============ Directory Listing ============ */

/* Directory entry - MUST MATCH kernel vfs_dirent_t (include/fs/vfs.h) */
#define NX_DIRENT_NAME_MAX 255
typedef struct {
    char name[NX_DIRENT_NAME_MAX + 1];
    uint32_t type;          /* 1 = file, 2 = directory */
    uint64_t inode;
    uint64_t size;
} nx_dirent_t;

#define NX_DIRENT_FILE      1
#define NX_DIRENT_DIR       2

/* List up to max entries of a directory. Returns 0 and sets *count, or < 0. */
static inline int nx_readdir(const char *path, nx_dirent_t *entries, uint32_t max, uint32_t *count) {
    return (int)syscall4(SYS_READDIR, (int64_t)path, (int64_t)entries, (int64_t)max, (int64_t)count);
}

/* ============ File I/O Wrappers ============ */

/* Open a file, returns fd or negative error */
static inline int nx_open(const char *path, int flags) {
    return (int)syscall3(SYS_OPEN, (int64_t)path, (int64_t)flags, 0);
}

/* Close a file descriptor */
static inline int nx_close(int fd) {
    return (int)syscall1(SYS_CLOSE, (int64_t)fd);
}

/* Read from file, returns bytes read or negative error */
static inline int64_t nx_read(int fd, void *buf, uint64_t count) {
    return syscall3(SYS_READ, (int64_t)fd, (int64_t)buf, (int64_t)count);
}

/* Write to file, returns bytes written or negative error */
static inline int64_t nx_write(int fd, const void *buf, uint64_t count) {
    return syscall3(SYS_WRITE, (int64_t)fd, (int64_t)buf, (int64_t)count);
}

/* Create a directory (parent must exist), returns 0 or negative error.
 * An existing directory is also reported as an error. */
static inline int nx_mkdir(const char *path) {
    return (int)syscall2(SYS_MKDIR, (int64_t)path, 0755);
}

/* Filesystem results returned by mkdir, remove and rename (kernel VFS codes) */
#define NX_FS_OK            0
#define NX_FS_NOT_FOUND    (-1)
#define NX_FS_PROTECTED    (-2)   /* System Integrity Protection refused it */
#define NX_FS_IO           (-3)   /* includes "this filesystem cannot do that" */
#define NX_FS_EXISTS       (-5)
#define NX_FS_NOT_DIR      (-6)
#define NX_FS_NOT_EMPTY    (-8)
#define NX_FS_BUSY         (-9)   /* open, or a mount point */
#define NX_FS_INVALID     (-10)   /* bad name, across volumes, or into itself */
#define NX_FS_READ_ONLY   (-12)   /* read-only file or volume */

/* Delete a file or an empty directory. Returns NX_FS_OK or an NX_FS_* error. */
static inline int nx_remove(const char *path) {
    return (int)syscall1(SYS_REMOVE, (int64_t)path);
}

/* Rename or move within one volume. The target must not exist.
 * Returns NX_FS_OK or an NX_FS_* error (NX_FS_INVALID across volumes). */
static inline int nx_rename(const char *old_path, const char *new_path) {
    return (int)syscall2(SYS_RENAME, (int64_t)old_path, (int64_t)new_path);
}

/* Seek in file, returns new position or negative error */
static inline int64_t nx_seek(int fd, int64_t offset, int whence) {
    return syscall3(SYS_SEEK, (int64_t)fd, offset, (int64_t)whence);
}

/* ============ API Wrappers ============ */

/* Get framebuffer info */
static inline int nx_fb_info(fb_info_t *info) {
    return (int)syscall1(SYS_FB_INFO, (int64_t)info);
}

/* Map framebuffer to userspace (returns address) */
static inline void* nx_fb_map(void) {
    return (void*)syscall0(SYS_FB_MAP);
}

/* Flip framebuffer (kernel does VSync wait + copy) */
static inline int nx_fb_flip(void *backbuffer) {
    return (int)syscall1(SYS_FB_FLIP, (int64_t)backbuffer);
}

/* Present only some rectangles of the backbuffer (ABI 1.2, system
 * processes only; spec: docs/desktop/proposals/nxserver-window-protocol.md).
 * The backbuffer has the same layout fb.flip takes. Returns 0 or a
 * negative errno (-EPERM for apps, -EINVAL for 0 or > NX_FB_RECTS_MAX
 * rectangles, -EFAULT for bad pointers). */
#include "nx_fb.h"
#define SYS_FB_FLIP_RECTS 148
static inline int nx_fb_flip_rects(const void *backbuffer, const nx_fb_rect_t *rects, uint32_t count) {
    return (int)syscall3(SYS_FB_FLIP_RECTS, (int64_t)backbuffer, (int64_t)rects, (int64_t)count);
}

/* Set cursor position (kernel cursor compositor) */
#define SYS_CURSOR_SET 128
static inline int nx_cursor_set(int x, int y, int visible) {
    return (int)syscall3(SYS_CURSOR_SET, (int64_t)x, (int64_t)y, (int64_t)visible);
}

/* Poll for input events */
static inline int nx_input_poll(input_event_t *event) {
    /* SYS_INPUT_POLL takes (ptr, max_events) */
    return (int)syscall2(SYS_INPUT_POLL, (int64_t)event, 1);
}

/* Register for input events */
static inline int nx_input_register(int type) {
    return (int)syscall1(SYS_INPUT_REGISTER, type);
}

/* Memory allocation via brk() syscall - Windows/macOS style heap management.
 * Uses kernel's 64MB heap reserve set up at boot.
 * This replaces the old 512KB static buffer. */

/* Internal: call brk syscall */
static inline int64_t nx_brk(uint64_t addr) {
    return (int64_t)syscall1(SYS_BRK, addr);
}

/* Heap allocator - implemented in nx_malloc.c with proper global state
 * NOTE: These MUST NOT be inline functions with file-local statics,
 * because each translation unit would get its own heap_end, causing
 * allocations from different files to collide!
 */
extern void* nx_malloc(uint64_t size);           /* NULL when size is 0 or the heap is full */
extern void nx_mfree(void *ptr);                  /* NULL is ignored; invalid/double frees are refused and logged */
extern void* nx_calloc(uint64_t count, uint64_t size);   /* zeroed; NULL on overflow */
extern void* nx_realloc(void *ptr, uint64_t size);       /* keeps contents; on failure ptr stays valid */
extern void nx_malloc_usage(uint64_t *in_use, uint64_t *heap_size);   /* bytes, headers included */

/* Legacy compatibility wrappers */
#define nx_alloc(size) nx_malloc(size)
#define nx_free(ptr) nx_mfree(ptr)

/* Get system time (milliseconds since boot) */
static inline uint64_t nx_gettime(void) {
    return (uint64_t)syscall0(SYS_TIME);
}

/* Sleep for milliseconds */
static inline int nx_sleep(uint64_t ms) {
    return (int)syscall1(SYS_SLEEP, ms);
}

/* Exit process */
static inline void nx_exit(int code) {
    syscall1(SYS_EXIT, code);
    /* Should not return */
    while(1) {}
}

/* Yield to scheduler */
static inline void nx_yield(void) {
    syscall0(SYS_YIELD);
}

/* Debug print to serial (uses SYS_DEBUG_PRINT syscall 127) */
static inline void nx_debug_print(const char *msg) {
    /* SYS_DEBUG_PRINT = 127 - prints directly to serial console */
    syscall1(127, (int64_t)msg);
}

/* ============ Handles, shared memory and IPC (ABI 1.1) ============ */
/*
 * Spec: docs/desktop/proposals/kernel-shm-ipc.md. Types and limits come
 * from the kernel's public header. Every call returns a negative errno on
 * failure; none of them blocks except nx_obj_wait with a timeout.
 */
#include "nx_ipc.h"

#define SYS_HANDLE_CLOSE  140
#define SYS_SHM_CREATE    141
#define SYS_SHM_MAP       142
#define SYS_SHM_UNMAP     143
#define SYS_CHAN_CREATE   144
#define SYS_CHAN_SEND     145
#define SYS_CHAN_RECV     146
#define SYS_OBJ_WAIT      147
#define SYS_PORT_LISTEN   150
#define SYS_PORT_CONNECT  151
#define SYS_PORT_ACCEPT   152

static inline int nx_handle_close(nx_handle_t h) {
    return (int)syscall1(SYS_HANDLE_CLOSE, (int64_t)h);
}
/* Map a whole shm object; *out_size (may be NULL) receives its size.
 * Returns the address as a non-negative value, or a negative errno. */
static inline int64_t nx_shm_map(nx_handle_t h, uint32_t prot, uint64_t *out_size) {
    return syscall3(SYS_SHM_MAP, (int64_t)h, (int64_t)prot, (int64_t)out_size);
}
static inline int nx_shm_unmap(void *addr) {
    return (int)syscall1(SYS_SHM_UNMAP, (int64_t)addr);
}
static inline int nx_chan_send(nx_handle_t h, const nx_chan_msg_t *msg) {
    return (int)syscall2(SYS_CHAN_SEND, (int64_t)h, (int64_t)msg);
}
static inline int nx_chan_recv(nx_handle_t h, nx_chan_msg_t *msg) {
    return (int)syscall2(SYS_CHAN_RECV, (int64_t)h, (int64_t)msg);
}
/* Returns the number of ready items, or a negative errno: -ETIMEDOUT (-110)
 * when nothing became ready in time (also for timeout 0). */
static inline int nx_obj_wait(nx_wait_item_t *items, uint32_t count, uint64_t timeout_ms) {
    return (int)syscall3(SYS_OBJ_WAIT, (int64_t)items, (int64_t)count, (int64_t)timeout_ms);
}
static inline nx_handle_t nx_port_listen(const char *name) {
    return (nx_handle_t)syscall2(SYS_PORT_LISTEN, (int64_t)name, 0);
}
static inline nx_handle_t nx_port_accept(nx_handle_t port, uint32_t *out_pid) {
    return (nx_handle_t)syscall2(SYS_PORT_ACCEPT, (int64_t)port, (int64_t)out_pid);
}
/* Client side (libnxwindow and apps). Handles are > 0; failures are a
 * negative errno. */
#define SYS_HANDLE_DUP    149
static inline nx_handle_t nx_shm_create(uint64_t size, uint32_t flags) {
    return (nx_handle_t)syscall2(SYS_SHM_CREATE, (int64_t)size, (int64_t)flags);
}
static inline nx_handle_t nx_port_connect(const char *name) {
    return (nx_handle_t)syscall2(SYS_PORT_CONNECT, (int64_t)name, 0);
}
static inline nx_handle_t nx_handle_dup(nx_handle_t h, uint32_t rights) {
    return (nx_handle_t)syscall2(SYS_HANDLE_DUP, (int64_t)h, (int64_t)rights);
}

/* ============ ABI query (syscall 0) ============ */
#include "nx_abi.h"
#define SYS_ABI_QUERY     0
/* Fills *info (kernel version and NX_ABI_FLAG_* capabilities). 0 or a
 * negative errno. */
static inline int nx_abi_query(nx_abi_info_t *info) {
    return (int)syscall1(SYS_ABI_QUERY, (int64_t)info);
}

/* ============ RTC (Real-Time Clock) ============ */
/* Syscall numbers - MUST MATCH kernel */
#define SYS_RTC_GET     130
#define SYS_RTC_SET     131
#define SYS_RTC_UNIX    132

/* RTC time structure - MUST MATCH kernel's rtc_time_t */
typedef struct {
    uint16_t year;
    uint8_t  month;
    uint8_t  day;
    uint8_t  hour;
    uint8_t  minute;
    uint8_t  second;
    uint8_t  weekday;
} __attribute__((packed)) rtc_time_t;

/* Get real-time clock value */
static inline int nx_rtc_get(rtc_time_t *time) {
    return (int)syscall1(SYS_RTC_GET, (int64_t)time);
}

/* Set real-time clock value (requires privilege) */
static inline int nx_rtc_set(const rtc_time_t *time) {
    return (int)syscall1(SYS_RTC_SET, (int64_t)time);
}

/* Get Unix timestamp (seconds since 1970-01-01) */
static inline uint64_t nx_rtc_unix(void) {
    return (uint64_t)syscall0(SYS_RTC_UNIX);
}

/* ============ Image Loading (Userspace Only) ============ */
/* 
 * Image loading is handled entirely in userspace via libnximage.
 * The kernel only provides file I/O (open/read/close) and memory (brk).
 * 
 * Include "nximage.h" and link with libnximage.a for:
 *   nxi_load(path, &img)   - Load image to RGBA32 buffer
 *   nxi_info(path, &w, &h) - Get dimensions without loading pixels
 *   nxi_free(&img)         - Free pixel buffer
 */

#endif /* NXSYSCALL_H */
