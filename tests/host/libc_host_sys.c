/*
 * nxlibc host test shim: stands in for the NeolyxOS kernel so the real
 * library code (sdk/libc, everything but src/nx/syscall.c) runs unchanged
 * on the x86_64 Linux development machine.
 *
 * Only the system calls nxlibc itself uses are provided. Each one is built
 * on a raw Linux system call and returns what the NeolyxOS kernel returns,
 * including its error conventions:
 *
 *   NXC_TEST_FILEIO unset   the kernel as it is today (ABI 1.4): file
 *                           errors are -errno for open/stat/close and VFS
 *                           codes (-1..-12) for read/write/seek/mkdir/
 *                           rmdir/rename; no standard descriptors.
 *   NXC_TEST_FILEIO=1       the S3 file-I/O ABI (NX_ABI_FLAG_FILEIO): every
 *                           file call returns -errno; fds 0-2 are open.
 *   NXC_TEST_HEAP_MB=n      size of the brk reserve (default 64, as the
 *                           kernel), for out-of-memory tests.
 *
 * The test programs are linked without the host C library:
 *   ld -static -nostdlib libc_host_sys.o test.o libnxc_host.a
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include <stddef.h>
#include <stdint.h>
#include <errno.h>
#include <nx/sys.h>

/* ---- Linux x86_64 system calls ---- */

#define LX_READ           0
#define LX_WRITE          1
#define LX_OPEN           2
#define LX_CLOSE          3
#define LX_STAT           4
#define LX_LSEEK          8
#define LX_MMAP           9
#define LX_MPROTECT       10
#define LX_NANOSLEEP      35
#define LX_GETPID         39
#define LX_RENAME         82
#define LX_MKDIR          83
#define LX_RMDIR          84
#define LX_UNLINK         87
#define LX_CLOCK_GETTIME  228
#define LX_EXIT_GROUP     231

#define LX_O_RDONLY  0
#define LX_O_WRONLY  01
#define LX_O_RDWR    02
#define LX_O_CREAT   0100
#define LX_O_TRUNC   01000

#define LX_PROT_NONE  0
#define LX_PROT_RW    3
#define LX_MAP_PRIVATE 0x02
#define LX_MAP_FIXED   0x10
#define LX_MAP_ANON    0x20
#define LX_MAP_NORESERVE 0x4000

/* Linux errno values that differ from NeolyxOS or need a decision */
#define LX_ERANGE  34
#define LX_ELOOP   40

static long lx(long n, long a1, long a2, long a3, long a4, long a5, long a6) {
    long r;
    register long r10 __asm__("r10") = a4;
    register long r8 __asm__("r8") = a5;
    register long r9 __asm__("r9") = a6;
    __asm__ volatile("syscall"
                     : "=a"(r)
                     : "a"(n), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8), "r"(r9)
                     : "rcx", "r11", "memory");
    return r;
}
#define LX0(n)                lx((n), 0, 0, 0, 0, 0, 0)
#define LX1(n, a)             lx((n), (long)(a), 0, 0, 0, 0, 0)
#define LX2(n, a, b)          lx((n), (long)(a), (long)(b), 0, 0, 0, 0)
#define LX3(n, a, b, c)       lx((n), (long)(a), (long)(b), (long)(c), 0, 0, 0)

/* ---- process entry ---- */

/* The Linux kernel starts a static program with RSP -> argc, argv[], NULL,
 * envp[], NULL. NeolyxOS passes argc/argv/envp in RDI/RSI/RDX; translate
 * and enter nxlibc exactly as crt/crt0.S does. */
_Noreturn void __nxc_start(long argc, char **argv, char **envp);
_Noreturn void __host_start(long *sp);

__asm__(
    ".text\n"
    ".globl _start\n"
    ".type _start,@function\n"
    "_start:\n"
    "    xor %ebp, %ebp\n"
    "    mov %rsp, %rdi\n"
    "    and $-16, %rsp\n"
    "    call __host_start\n"
    "    hlt\n");

static char **g_envp;
static int    g_fileio;
static uint64_t g_heap_reserve = 64ull << 20;

static int env_is(const char *entry, const char *name, const char **value) {
    while (*name && *entry == *name) { entry++; name++; }
    if (*name || *entry != '=') return 0;
    *value = entry + 1;
    return 1;
}

static const char *host_getenv(const char *name) {
    for (char **e = g_envp; e && *e; e++) {
        const char *v;
        if (env_is(*e, name, &v)) return v;
    }
    return NULL;
}

_Noreturn void __host_start(long *sp) {
    long argc = sp[0];
    char **argv = (char **)(sp + 1);
    g_envp = argv + argc + 1;
    const char *v = host_getenv("NXC_TEST_FILEIO");
    g_fileio = (v && v[0] == '1');
    v = host_getenv("NXC_TEST_HEAP_MB");
    if (v) {
        uint64_t mb = 0;
        while (*v >= '0' && *v <= '9') mb = mb * 10 + (uint64_t)(*v++ - '0');
        if (mb >= 1 && mb <= 4096) g_heap_reserve = mb << 20;
    }
    /* NeolyxOS passes envp only inline after argv; today the kernel
     * passes none, which the tests do not depend on */
    __nxc_start(argc, argv, g_envp);
}

/* ---- error conventions ---- */

static int nx_errno(long lx_err) {
    switch (lx_err) {
    case LX_ERANGE: return ERANGE;
    case LX_ELOOP:  return EINVAL;
    default:
        if (lx_err >= 1 && lx_err <= 39) return (int)lx_err;  /* same numbers */
        return EIO;
    }
}

/* Today's kernel turns VFS failures into VFS codes, not errno */
static long vfs_code(long lx_err) {
    switch (nx_errno(lx_err)) {
    case ENOENT:    return -1;
    case EPERM:
    case EACCES:    return -2;
    case ENOMEM:    return -4;
    case EEXIST:    return -5;
    case ENOTDIR:   return -6;
    case EISDIR:    return -7;
    case ENOTEMPTY: return -8;
    case EBUSY:     return -9;
    case EINVAL:    return -10;
    case ENOSPC:    return -11;
    case EROFS:     return -12;
    default:        return -3;
    }
}

/* -errno style result (open, stat, close; everything with FILEIO) */
static long ret_errno(long r) { return (r < 0) ? -(long)nx_errno(-r) : r; }

/* read/write/seek: -EBADF / -EFAULT, or a VFS code */
static long ret_rw(long r) {
    if (r >= 0 || g_fileio) return ret_errno(r);
    if (r == -9) return -EBADF;
    if (r == -14) return -EFAULT;
    return vfs_code(-r);
}

/* readdir/mkdir/rmdir/rename: -EFAULT or a VFS code */
static long ret_vfs(long r) {
    if (r >= 0 || g_fileio) return ret_errno(r);
    if (r == -14) return -EFAULT;
    return vfs_code(-r);
}

/* ---- ABI ---- */

long nxsys_abi_query(nxsys_abi_info_t *info) {
    if (!info) return -EFAULT;
    info->major = 1;
    info->minor = 4;
    info->patch = 0;
    info->flags = (uint16_t)(NXSYS_ABI_FLAG_SHM | NXSYS_ABI_FLAG_IPC | NXSYS_ABI_FLAG_FB_RECTS |
                             NXSYS_ABI_FLAG_MEMCLASS | NXSYS_ABI_FLAG_NET |
                             (g_fileio ? NXSYS_ABI_FLAG_FILEIO : 0));
    info->syscall_count = 0;
    info->reserved[0] = info->reserved[1] = info->reserved[2] = 0;
    return 0;
}

/* ---- process ---- */

_Noreturn void nxsys_exit(int code) {
    for (;;) LX1(LX_EXIT_GROUP, code);
}

struct lx_timespec { long sec; long nsec; };

uint64_t nxsys_time(void) {
    struct lx_timespec ts;
    if (LX2(LX_CLOCK_GETTIME, 1 /* MONOTONIC */, &ts) != 0) return 0;
    return (uint64_t)ts.sec * 1000u + (uint64_t)ts.nsec / 1000000u;
}

/* Test hook: make rtc.unix answer like a kernel whose clock was never
 * set up (a negative value), as kernels before the RTC fix do */
static int g_rtc_broken;
void nxc_host_rtc_broken(int on);
void nxc_host_rtc_broken(int on) { g_rtc_broken = on; }

uint64_t nxsys_rtc_unix(void) {
    if (g_rtc_broken) return (uint64_t)-86365LL;
    struct lx_timespec ts;
    if (LX2(LX_CLOCK_GETTIME, 0 /* REALTIME */, &ts) != 0) return 0;
    return (uint64_t)ts.sec;
}

long nxsys_sleep(uint32_t ms) {
    struct lx_timespec ts = { (long)(ms / 1000u), (long)(ms % 1000u) * 1000000L };
    return ret_errno(LX2(LX_NANOSLEEP, &ts, 0));
}

long nxsys_getpid(void) { return LX0(LX_GETPID); }

/* brk: a fixed reserve; growing maps zeroed pages, shrinking drops them
 * (replacing the range with fresh inaccessible pages), as the kernel does */
static uintptr_t g_heap_start, g_heap_end, g_heap_mapped;

static int heap_reserve(void) {
    if (g_heap_start) return 0;
    long r = lx(LX_MMAP, 0, (long)g_heap_reserve, LX_PROT_NONE,
                LX_MAP_PRIVATE | LX_MAP_ANON | LX_MAP_NORESERVE, -1, 0);
    if (r < 0) return -1;
    g_heap_start = g_heap_end = g_heap_mapped = (uintptr_t)r;
    return 0;
}

long nxsys_brk(void *addr) {
    if (heap_reserve() != 0) return -ENOMEM;
    uintptr_t a = (uintptr_t)addr;
    if (a == 0) return (long)g_heap_end;
    if (a < g_heap_start) return -EINVAL;
    if (a > g_heap_start + g_heap_reserve) return -ENOMEM;
    uintptr_t aligned = (a + 4095u) & ~(uintptr_t)4095u;
    if (aligned > g_heap_mapped) {
        if (LX3(LX_MPROTECT, g_heap_mapped, aligned - g_heap_mapped, LX_PROT_RW) != 0)
            return -ENOMEM;
        g_heap_mapped = aligned;
    } else if (aligned < g_heap_mapped) {
        long r = lx(LX_MMAP, (long)aligned, (long)(g_heap_mapped - aligned), LX_PROT_NONE,
                    LX_MAP_PRIVATE | LX_MAP_ANON | LX_MAP_FIXED | LX_MAP_NORESERVE, -1, 0);
        if (r < 0) return -ENOMEM;
        g_heap_mapped = aligned;
    }
    g_heap_end = aligned;
    return (long)aligned;
}

/* ---- files ---- */

long nxsys_open(const char *path, uint32_t flags, uint32_t mode) {
    (void)mode;
    if (!path) return -EFAULT;
    long lf;
    switch (flags & 3u) {
    case NXSYS_O_RDONLY: lf = LX_O_RDONLY; break;
    case NXSYS_O_WRONLY: lf = LX_O_WRONLY; break;
    case NXSYS_O_RDWR:   lf = LX_O_RDWR; break;
    default:             return -EINVAL;
    }
    if (flags & NXSYS_O_CREATE) lf |= LX_O_CREAT;
    if (flags & NXSYS_O_TRUNC)  lf |= LX_O_TRUNC;
    long fd = LX3(LX_OPEN, path, lf, 0644);
    if (fd < 0) return ret_errno(fd);
    if (flags & NXSYS_O_APPEND) {
        /* the kernel only starts the offset at the end */
        if (LX3(LX_LSEEK, fd, 0, 2) < 0) {
            LX1(LX_CLOSE, fd);
            return -EIO;
        }
    }
    return fd;
}

long nxsys_close(int fd) { return ret_errno(LX1(LX_CLOSE, fd)); }

long nxsys_read(int fd, void *buf, size_t count) {
    if (!buf) return -EFAULT;
    return ret_rw(LX3(LX_READ, fd, buf, count));
}

long nxsys_write(int fd, const void *buf, size_t count) {
    if (!buf) return -EFAULT;
    return ret_rw(LX3(LX_WRITE, fd, buf, count));
}

long nxsys_seek(int fd, int64_t offset, int whence) {
    if (whence < 0 || whence > 2) return g_fileio ? -EINVAL : -10;
    return ret_rw(LX3(LX_LSEEK, fd, offset, whence));
}

struct lx_stat {
    uint64_t dev, ino, nlink;
    uint32_t mode, uid, gid, pad0;
    uint64_t rdev;
    int64_t  size, blksize, blocks;
    uint64_t atime, atime_ns, mtime, mtime_ns, ctime, ctime_ns;
    int64_t  unused[3];
};
_Static_assert(sizeof(struct lx_stat) == 144, "Linux x86_64 struct stat");

long nxsys_stat(const char *path, nxsys_stat_t *st) {
    if (!path || !st) return -EFAULT;
    struct lx_stat ls;
    long r = LX2(LX_STAT, path, &ls);
    if (r < 0) return ret_errno(r);
    st->st_ino = ls.ino;
    st->st_mode = ls.mode;
    st->st_nlink = (uint32_t)ls.nlink;
    st->st_uid = ls.uid;
    st->st_gid = ls.gid;
    st->st_size = (uint64_t)ls.size;
    st->st_atime = ls.atime;
    st->st_mtime = ls.mtime;
    st->st_ctime = ls.ctime;
    return 0;
}

long nxsys_mkdir(const char *path, uint32_t mode) {
    if (!path) return -EFAULT;
    return ret_vfs(LX2(LX_MKDIR, path, mode ? mode : 0755));
}

/* The kernel's rmdir removes a file or an empty directory */
long nxsys_rmdir(const char *path) {
    if (!path) return -EFAULT;
    long r = LX1(LX_UNLINK, path);
    if (r == -21 /* EISDIR */) r = LX1(LX_RMDIR, path);
    return ret_vfs(r);
}

long nxsys_rename(const char *oldpath, const char *newpath) {
    if (!oldpath || !newpath) return -EFAULT;
    return ret_vfs(LX2(LX_RENAME, oldpath, newpath));
}

/* ---- debug log: stderr of the host process ---- */

long nxsys_debug_print(const char *msg) {
    if (!msg) return -EFAULT;
    size_t n = 0;
    while (n < 255 && msg[n]) n++;   /* the kernel copies at most 255 bytes */
    long r = LX3(LX_WRITE, 2, msg, n);
    return (r < 0) ? -EIO : 0;
}
