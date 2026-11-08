/*
 * nxlibc - <nx/sys.h>: NeolyxOS native system calls
 *
 * One C function per registered syscall (kernel/include/core/
 * syscall_table.def), named nxsys_<name> with the dots of the registered
 * name turned into underscores ("fb.flip_rects" -> nxsys_fb_flip_rects).
 *
 * Conventions
 *   - Every function returns what the kernel returned: >= 0 on success, a
 *     negative error number (-ENOENT, ...) on failure. errno is NOT set;
 *     the ISO C layer (stdio, stdlib, ...) does that.
 *   - Calls that return an address (brk, fb.map, shm.map) return it as a
 *     long; negative values are errors.
 *   - A syscall the running kernel does not have returns -ENOSYS. Feature
 *     groups added after ABI 1.0 are announced by a flag in
 *     nxsys_abi_info_t.flags (NXSYS_ABI_FLAG_*); test the flag with
 *     nxsys_abi_has() before relying on the group.
 *   - Structures here are copies of the kernel's, renamed with an nxsys_
 *     prefix so this header never collides with the kernel's or the
 *     desktop's headers. tests/host/libc_abi_check.c compares every size,
 *     offset, number and constant with the kernel headers.
 *
 * File syscalls on kernels without NXSYS_ABI_FLAG_FILEIO return some
 * failures as raw VFS codes (-1 .. -12) rather than -errno. Use
 * nxsys_file_errno(rc, NXSYS_<call>) to turn any file-syscall result into
 * an errno value; the ISO C layer does this already.
 *
 * Syscalls that stream S3 adds for the beta (mmap, fstat, dup, pipe,
 * spawn_ex, pty, proc.*) are added here when their ABI is signed off.
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#ifndef NXLIBC_NX_SYS_H
#define NXLIBC_NX_SYS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============ Syscall numbers (STABLE, from syscall_table.def) ============ */

#define NXSYS_ABI_QUERY          0
#define NXSYS_EXIT               1
#define NXSYS_SLEEP              2
#define NXSYS_TIME               3
#define NXSYS_YIELD              4
#define NXSYS_GETPID             5
#define NXSYS_GETPPID            6
#define NXSYS_FORK               7
#define NXSYS_EXEC               8
#define NXSYS_WAIT               9
#define NXSYS_BRK                10
#define NXSYS_SPAWN              13
#define NXSYS_OPEN               20
#define NXSYS_CLOSE              21
#define NXSYS_READ               22
#define NXSYS_WRITE              23
#define NXSYS_SEEK               24
#define NXSYS_STAT               25
#define NXSYS_READDIR            30
#define NXSYS_MKDIR              31
#define NXSYS_RMDIR              32
#define NXSYS_CHDIR              33
#define NXSYS_GETCWD             34
#define NXSYS_RENAME             35
#define NXSYS_SOCKET             40
#define NXSYS_BIND               41
#define NXSYS_LISTEN             42
#define NXSYS_ACCEPT             43
#define NXSYS_CONNECT            44
#define NXSYS_SEND               45
#define NXSYS_RECV               46
#define NXSYS_CLOSESOCK          47
#define NXSYS_INFO               50
#define NXSYS_REBOOT             100
#define NXSYS_SHUTDOWN           101
#define NXSYS_NXGAME_CREATE      110
#define NXSYS_NXGAME_DESTROY     111
#define NXSYS_NXGAME_START       112
#define NXSYS_NXGAME_STOP        113
#define NXSYS_NXGAME_GPU_ACQUIRE 114
#define NXSYS_NXGAME_GPU_RELEASE 115
#define NXSYS_NXGAME_GPU_PRESENT 116
#define NXSYS_NXGAME_AUDIO       117
#define NXSYS_NXGAME_INPUT       118
#define NXSYS_NXGAME_GET_BUFFER  119
#define NXSYS_FB_MAP             120
#define NXSYS_FB_INFO            121
#define NXSYS_FB_FLIP            122
#define NXSYS_INPUT_POLL         123
#define NXSYS_INPUT_REGISTER     124
#define NXSYS_DEBUG_PRINT        127
#define NXSYS_CURSOR_SET         128
#define NXSYS_RTC_GET            130
#define NXSYS_RTC_SET            131
#define NXSYS_RTC_UNIX           132
#define NXSYS_NET_IF_LIST        133
#define NXSYS_HANDLE_CLOSE       140
#define NXSYS_SHM_CREATE         141
#define NXSYS_SHM_MAP            142
#define NXSYS_SHM_UNMAP          143
#define NXSYS_CHAN_CREATE        144
#define NXSYS_CHAN_SEND          145
#define NXSYS_CHAN_RECV          146
#define NXSYS_OBJ_WAIT           147
#define NXSYS_FB_FLIP_RECTS      148
#define NXSYS_HANDLE_DUP         149
#define NXSYS_PORT_LISTEN        150
#define NXSYS_PORT_CONNECT       151
#define NXSYS_PORT_ACCEPT        152
#define NXSYS_PROC_SET_CLASS     153
#define NXSYS_MEM_PRESSURE_OPEN  154
#define NXSYS_MEM_PRESSURE_READ  155
#define NXSYS_MEM_STATS          156

/* ============ ABI query (0) ============ */

typedef struct {
    uint16_t major;          /* breaking changes */
    uint16_t minor;          /* new features */
    uint16_t patch;          /* fixes */
    uint16_t flags;          /* NXSYS_ABI_FLAG_* */
    uint32_t syscall_count;  /* registered syscalls */
    uint32_t reserved[3];
} nxsys_abi_info_t;

#define NXSYS_ABI_FLAG_SHM       0x0001u  /* shm.*, handle.* */
#define NXSYS_ABI_FLAG_IPC       0x0002u  /* chan.*, obj.wait, port.* */
#define NXSYS_ABI_FLAG_FB_RECTS  0x0004u  /* fb.flip_rects */
#define NXSYS_ABI_FLAG_MEMCLASS  0x0008u  /* proc.set_class, mem.* */
#define NXSYS_ABI_FLAG_NET       0x0020u  /* net.if_list */
#define NXSYS_ABI_FLAG_FILEIO    0x0040u  /* S3: errno-only file calls, std fds 0-2, mmap, ... */
#define NXSYS_ABI_FLAG_PTY       0x0080u  /* S3: pseudo-terminals */
#define NXSYS_ABI_FLAG_PROC      0x0100u  /* S3: proc.list, sys.stats, proc.kill */

long nxsys_abi_query(nxsys_abi_info_t *info);

/* Library helpers (not syscalls). The first call queries the kernel; the
 * answer is cached for the life of the process. */
int      nxsys_abi_has(uint16_t flag);          /* 1 if every bit of flag is set */
uint16_t nxsys_abi_flags(void);                 /* 0 if abi.query failed */

/* ============ Process (1-13) ============ */

#define NXSYS_SPAWN_WAITABLE 0x1u   /* keep the child for nxsys_wait(); default detached */
#define NXSYS_ARGV_MAX       16     /* arguments the kernel copies (PROCESS_ARGV_MAX) */
#define NXSYS_ARG_LEN        256    /* bytes per argument, with the NUL (PROCESS_ARG_LEN) */

_Noreturn void nxsys_exit(int code);
long     nxsys_sleep(uint32_t ms);
uint64_t nxsys_time(void);                      /* milliseconds since boot */
long     nxsys_yield(void);
long     nxsys_getpid(void);
long     nxsys_getppid(void);
long     nxsys_fork(void);                      /* -ENOSYS: use nxsys_spawn */
long     nxsys_exec(const char *path, char *const argv[], char *const envp[]);
long     nxsys_wait(long pid, int *status);
long     nxsys_brk(void *addr);                 /* addr NULL: query; returns the break */
long     nxsys_spawn(const char *path, char *const argv[], uint32_t flags);

/* ============ Files and directories (20-35) ============ */

/* nxsys_open flags (the kernel's VFS_O_*). Read-only is 1, not 0. */
#define NXSYS_O_RDONLY   0x0001u
#define NXSYS_O_WRONLY   0x0002u
#define NXSYS_O_RDWR     0x0003u
#define NXSYS_O_CREATE   0x0100u
#define NXSYS_O_TRUNC    0x0200u
#define NXSYS_O_APPEND   0x0400u    /* the offset starts at the end */

#define NXSYS_SEEK_SET   0
#define NXSYS_SEEK_CUR   1
#define NXSYS_SEEK_END   2

#define NXSYS_PATH_MAX   256        /* bytes, with the NUL */
#define NXSYS_NAME_MAX   255

/* st_mode type bits */
#define NXSYS_S_IFMT     0170000u
#define NXSYS_S_IFDIR    0040000u
#define NXSYS_S_IFREG    0100000u

typedef struct {
    uint64_t st_ino;
    uint32_t st_mode;       /* NXSYS_S_IF* | permission bits */
    uint32_t st_nlink;
    uint32_t st_uid;
    uint32_t st_gid;
    uint64_t st_size;
    uint64_t st_atime;
    uint64_t st_mtime;
    uint64_t st_ctime;
} nxsys_stat_t;

/* nxsys_dirent_t.type */
#define NXSYS_DT_FILE    1
#define NXSYS_DT_DIR     2
#define NXSYS_DT_SYMLINK 3
#define NXSYS_DT_DEVICE  4
#define NXSYS_DT_PIPE    5
#define NXSYS_DT_SOCKET  6

typedef struct {
    char     name[NXSYS_NAME_MAX + 1];
    int32_t  type;          /* NXSYS_DT_* */
    uint64_t inode;
    uint64_t size;
} nxsys_dirent_t;

long nxsys_open(const char *path, uint32_t flags, uint32_t mode);
long nxsys_close(int fd);
long nxsys_read(int fd, void *buf, size_t count);
long nxsys_write(int fd, const void *buf, size_t count);
long nxsys_seek(int fd, int64_t offset, int whence);   /* new offset */
long nxsys_stat(const char *path, nxsys_stat_t *st);
/* Lists up to count entries of a directory; *nread receives how many */
long nxsys_readdir(const char *path, nxsys_dirent_t *entries, uint32_t count, uint32_t *nread);
long nxsys_mkdir(const char *path, uint32_t mode);
long nxsys_rmdir(const char *path);                      /* a file or an empty directory */
long nxsys_chdir(const char *path);
long nxsys_getcwd(char *buf, size_t size);               /* length without the NUL */
long nxsys_rename(const char *oldpath, const char *newpath);

/* errno value (> 0) for a negative result of file syscall `num`
 * (NXSYS_OPEN ... NXSYS_RENAME), 0 for rc >= 0 */
int nxsys_file_errno(long rc, int num);

/* ============ Sockets (40-47) ============ */

#define NXSYS_AF_INET      2
#define NXSYS_SOCK_STREAM  1        /* TCP */
#define NXSYS_SOCK_DGRAM   2        /* UDP */
#define NXSYS_SOCK_RAW     3

typedef struct {
    uint16_t family;        /* NXSYS_AF_INET */
    uint16_t port;          /* network byte order */
    uint8_t  addr[4];       /* network byte order */
    uint8_t  zero[8];
} nxsys_sockaddr_in_t;

long nxsys_socket(int type, int protocol);
long nxsys_bind(int fd, const nxsys_sockaddr_in_t *addr);
long nxsys_listen(int fd, int backlog);
long nxsys_accept(int fd, nxsys_sockaddr_in_t *addr);   /* addr may be NULL */
long nxsys_connect(int fd, const nxsys_sockaddr_in_t *addr);
long nxsys_send(int fd, const void *buf, size_t len);
long nxsys_recv(int fd, void *buf, size_t len);
long nxsys_closesock(int fd);

/* ============ System information and control (50, 100-101) ============ */

typedef struct {
    char     os_name[32];
    char     os_version[32];
    char     arch[16];
    uint64_t uptime_ms;
    uint64_t total_memory;
    uint64_t free_memory;
    uint32_t num_cpus;
    uint32_t num_processes;
} nxsys_sysinfo_t;

long nxsys_info(nxsys_sysinfo_t *info);
long nxsys_reboot(int cmd);                 /* system processes only */
long nxsys_shutdown(void);                  /* system processes only */

/* ============ NXGame bridge (110-119) ============ */
/* Session structures belong to the NXGame bridge and are passed as
 * opaque buffers of the sizes below. */

#define NXSYS_NXGAME_INPUT_STATE_SIZE  512
#define NXSYS_NXGAME_BUFFER_INFO_SIZE  64
#define NXSYS_NXGAME_AUDIO_ACQUIRE     0
#define NXSYS_NXGAME_AUDIO_RELEASE     1

long nxsys_nxgame_create(int type, uint32_t caps, const void *quotas);
long nxsys_nxgame_destroy(int session);
long nxsys_nxgame_start(int session);
long nxsys_nxgame_stop(int session);
long nxsys_nxgame_gpu_acquire(int session);
long nxsys_nxgame_gpu_release(int session);
long nxsys_nxgame_gpu_present(int session, int wait_vsync);
long nxsys_nxgame_audio(int session, int cmd, uint32_t sample_rate,
                        uint32_t channels, uint32_t buffer_frames);
long nxsys_nxgame_input(int session, void *state /* NXSYS_NXGAME_INPUT_STATE_SIZE */);
long nxsys_nxgame_get_buffer(int session, void *info /* NXSYS_NXGAME_BUFFER_INFO_SIZE */);

/* ============ Framebuffer, input, cursor, debug (120-128) ============ */

typedef struct {
    uint64_t base_addr;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;         /* bytes per row */
    uint32_t bpp;
    uint32_t format;        /* 0 = BGRA, 1 = RGBA */
    uint64_t size;
} nxsys_fb_info_t;

#define NXSYS_INPUT_KEYBOARD      0
#define NXSYS_INPUT_MOUSE_MOVE    1
#define NXSYS_INPUT_MOUSE_BUTTON  2

typedef struct {
    uint32_t type;          /* NXSYS_INPUT_* */
    uint32_t timestamp;     /* ms since boot */
    union {
        struct {
            uint8_t scancode;
            uint8_t ascii;
            uint8_t pressed;
            uint8_t modifiers;
        } keyboard;
        struct {
            int16_t dx;
            int16_t dy;
            uint8_t buttons;
            int8_t  wheel;
            uint8_t _pad[2];
        } mouse;
    } u;
} nxsys_input_event_t;

typedef struct {
    int32_t  x, y;
    uint32_t w, h;
} nxsys_fb_rect_t;

#define NXSYS_FB_RECTS_MAX 64

long nxsys_fb_map(nxsys_fb_info_t *info);        /* address of the framebuffer */
long nxsys_fb_info(nxsys_fb_info_t *info);
long nxsys_fb_flip(const void *backbuffer);
long nxsys_input_poll(nxsys_input_event_t *events, uint32_t max);
long nxsys_input_register(uint32_t types);
/* Writes msg (at most 255 bytes are used) to the kernel debug log */
long nxsys_debug_print(const char *msg);
long nxsys_cursor_set(int x, int y, int visible);

/* ============ Real-time clock (130-132) ============ */

typedef struct {
    uint16_t year;
    uint8_t  month;         /* 1-12 */
    uint8_t  day;           /* 1-31 */
    uint8_t  hour;
    uint8_t  minute;
    uint8_t  second;
    uint8_t  weekday;       /* 0 = Sunday */
} nxsys_rtc_time_t;

long     nxsys_rtc_get(nxsys_rtc_time_t *t);
long     nxsys_rtc_set(const nxsys_rtc_time_t *t);  /* system processes only */
uint64_t nxsys_rtc_unix(void);                      /* seconds since 1970-01-01 UTC */

/* ============ Handles, shared memory and IPC (140-152, ABI 1.1/1.2) ============ */
/* Spec: docs/desktop/proposals/kernel-shm-ipc.md */

typedef int32_t nxsys_handle_t;             /* > 0 valid; 0 is never a handle */

#define NXSYS_RIGHT_READ         0x01u
#define NXSYS_RIGHT_WRITE        0x02u
#define NXSYS_RIGHT_MAP          0x04u
#define NXSYS_RIGHT_TRANSFER     0x08u
#define NXSYS_RIGHT_DUPLICATE    0x10u
#define NXSYS_RIGHTS_SAME        0x80000000u
#define NXSYS_SHM_PROT_READ      0x1u
#define NXSYS_SHM_PROT_WRITE     0x2u
#define NXSYS_SIGNAL_READABLE    0x1u
#define NXSYS_SIGNAL_WRITABLE    0x2u
#define NXSYS_SIGNAL_PEER_CLOSED 0x4u
#define NXSYS_WAIT_FOREVER       0xFFFFFFFFFFFFFFFFULL
#define NXSYS_CHAN_DATA_MAX      1024
#define NXSYS_CHAN_HANDLES_MAX   4
#define NXSYS_WAIT_ITEMS_MAX     64
#define NXSYS_PORT_NAME_MAX      31

typedef struct {
    uint64_t data;          /* pointer to the message bytes */
    uint64_t handles;       /* pointer to an nxsys_handle_t array */
    uint32_t data_len;
    uint32_t handle_count;
    uint32_t sender_pid;    /* recv: filled by the kernel; send: 0 */
    uint32_t flags;         /* 0 */
} nxsys_chan_msg_t;

typedef struct {
    nxsys_handle_t handle;
    uint32_t signals;       /* in */
    uint32_t observed;      /* out */
    uint32_t reserved;      /* 0 */
} nxsys_wait_item_t;

long nxsys_handle_close(nxsys_handle_t h);
long nxsys_shm_create(uint64_t size, uint32_t flags);
long nxsys_shm_map(nxsys_handle_t h, uint32_t prot, uint64_t *out_size);   /* address */
long nxsys_shm_unmap(void *addr);
long nxsys_chan_create(nxsys_handle_t out[2], uint32_t flags);
long nxsys_chan_send(nxsys_handle_t h, const nxsys_chan_msg_t *msg);
long nxsys_chan_recv(nxsys_handle_t h, nxsys_chan_msg_t *msg);
long nxsys_obj_wait(nxsys_wait_item_t *items, uint32_t count, uint64_t timeout_ms);
long nxsys_fb_flip_rects(const void *backbuffer, const nxsys_fb_rect_t *rects, uint32_t count);
long nxsys_handle_dup(nxsys_handle_t h, uint32_t rights);
long nxsys_port_listen(const char *name, uint32_t flags);
long nxsys_port_connect(const char *name, uint32_t flags);
long nxsys_port_accept(nxsys_handle_t port, uint32_t *out_pid);

/* ============ App classes and memory (153-156, ABI 1.3) ============ */

#define NXSYS_CLASS_FOREGROUND   1u
#define NXSYS_CLASS_BACKGROUND   2u
#define NXSYS_MEM_NORMAL         0u
#define NXSYS_MEM_WARN           1u
#define NXSYS_MEM_CRITICAL       2u
#define NXSYS_EXIT_RECLAIMED     251    /* exit status of an app ended to free memory */

typedef struct {
    uint32_t size;
    uint32_t version;
    uint32_t level;         /* NXSYS_MEM_* */
    uint32_t reserved;
    uint64_t total_bytes;
    uint64_t free_bytes;
    uint64_t reserve_bytes;
    uint64_t user_bytes;
    uint64_t shm_bytes;
    uint64_t kheap_bytes;
    uint64_t refusals;
    uint64_t self_bytes;
} nxsys_mem_stats_t;

long nxsys_proc_set_class(uint32_t pid, uint32_t cls);
long nxsys_mem_pressure_open(uint32_t flags);
long nxsys_mem_pressure_read(nxsys_handle_t h);
long nxsys_mem_stats(nxsys_mem_stats_t *st, uint32_t size);

/* ============ Network status (133, ABI 1.4) ============ */

#define NXSYS_NET_IF_MAX  8u
#define NXSYS_NET_DNS_MAX 3u

typedef struct {
    uint32_t size;
    uint32_t index;
    char     name[16];
    uint8_t  kind;
    uint8_t  state;
    uint8_t  link_up;
    uint8_t  primary;
    uint8_t  mac[6];
    uint8_t  ipv4_method;
    uint8_t  dns_count;
    uint32_t speed_mbps;
    uint8_t  addr[4];
    uint8_t  mask[4];
    uint8_t  gateway[4];
    uint8_t  dns[NXSYS_NET_DNS_MAX][4];
    uint32_t lease_seconds;
    uint32_t reserved;
    uint64_t rx_bytes, tx_bytes;
    uint64_t rx_packets, tx_packets;
    uint64_t rx_errors, tx_errors;
} nxsys_net_if_info_t;

/* Total interfaces (may exceed cap), or a negative error */
long nxsys_net_if_list(nxsys_net_if_info_t *buf, uint32_t cap, uint32_t elem_size);

/* ============ Raw entry ============ */

/* Calls syscall num with up to five arguments; for numbers this header
 * does not wrap yet. */
long nxsys_call(long num, long a1, long a2, long a3, long a4, long a5);

/* Layout checks: these layouts are part of the kernel ABI */
_Static_assert(sizeof(nxsys_abi_info_t) == 24, "nxsys_abi_info_t");
_Static_assert(sizeof(nxsys_stat_t) == 56, "nxsys_stat_t");
_Static_assert(sizeof(nxsys_dirent_t) == 280, "nxsys_dirent_t");
_Static_assert(sizeof(nxsys_sockaddr_in_t) == 16, "nxsys_sockaddr_in_t");
_Static_assert(sizeof(nxsys_fb_info_t) == 40, "nxsys_fb_info_t");
_Static_assert(sizeof(nxsys_input_event_t) == 16, "nxsys_input_event_t");
_Static_assert(sizeof(nxsys_fb_rect_t) == 16, "nxsys_fb_rect_t");
_Static_assert(sizeof(nxsys_rtc_time_t) == 8, "nxsys_rtc_time_t");
_Static_assert(sizeof(nxsys_chan_msg_t) == 32, "nxsys_chan_msg_t");
_Static_assert(sizeof(nxsys_wait_item_t) == 16, "nxsys_wait_item_t");
_Static_assert(sizeof(nxsys_mem_stats_t) == 80, "nxsys_mem_stats_t");
_Static_assert(sizeof(nxsys_net_if_info_t) == 120, "nxsys_net_if_info_t");

#ifdef __cplusplus
}
#endif

#endif /* NXLIBC_NX_SYS_H */
