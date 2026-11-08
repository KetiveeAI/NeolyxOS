/*
 * NeolyxOS File and Descriptor ABI (public)
 *
 * Spec: docs/kernel/proposals/beta-process-abi.md, sections 3-6
 * (approved by the owner 2026-11-01). Fixed-width types and no kernel
 * includes, so nxlibc, the desktop and the REOX runtime can use this header
 * as is. Layouts here are frozen once 1.0 ships.
 *
 * Syscalls (S3 block):
 *   14 dup        int32_t dup(int32_t fd)
 *   15 dup2       int32_t dup2(int32_t fd, int32_t new_fd)
 *   18 fd.ctl     int64_t fd_ctl(int32_t fd, uint32_t cmd, uint64_t arg)
 *   26 fstat      int fstat(int32_t fd, stat_t *st)
 *   27 unlink     int unlink(const char *path)
 *   28 ftruncate  int ftruncate(int32_t fd, uint64_t length)
 *   36 fsync      int fsync(int32_t fd)
 *   37 statfs     int statfs(const char *path, nx_statfs_t *buf)
 * and later in this group: 11 mmap, 12 munmap, 16 pipe, 17 spawn_ex
 * (core/nx_proc.h), 19 wait_ex (core/nx_proc.h).
 *
 * Test NX_ABI_FLAG_FILEIO in nx_abi_info_t.flags (syscall 0) before relying
 * on any of them. Until it is set, a call may return -ENOSYS.
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#ifndef NEOLYX_NX_FILE_H
#define NEOLYX_NX_FILE_H

#include <stdint.h>

/* abi.query flag: the whole FILEIO group (section 2) is available, and every
 * file, directory and path syscall (20-37) returns -errno only. */
#define NX_ABI_FLAG_FILEIO      0x0040u

/* Descriptors per process. open/dup/pipe/pty.open allocate from
 * NX_FD_FIRST_AUTO up; only dup2 and the spawn_ex fd map use 0-2. */
#define NX_FD_MAX               64
#define NX_FD_FIRST_AUTO        3

/* open (20) flags. The first six keep their existing values. */
#define NX_O_RDONLY             0x0001u
#define NX_O_WRONLY             0x0002u
#define NX_O_RDWR               0x0003u
#define NX_O_CREATE             0x0100u
#define NX_O_TRUNC              0x0200u
#define NX_O_APPEND             0x0400u
#define NX_O_EXCL               0x0800u     /* with CREATE: -EEXIST if the path exists */
#define NX_O_CLOEXEC            0x1000u     /* the fd is closed by exec */
#define NX_O_NONBLOCK           0x2000u     /* pipes and PTYs return -EAGAIN instead of blocking */
#define NX_O_ACCMODE            0x0003u
#define NX_O_VALID              0x3F03u     /* every bit open accepts */

/* fd.ctl (18) commands */
#define NX_FD_GET_FLAGS         1u          /* returns fd flags */
#define NX_FD_SET_FLAGS         2u          /* arg: fd flags */
#define NX_FD_GET_STATUS        3u          /* returns access mode | NX_O_APPEND | NX_O_NONBLOCK */
#define NX_FD_SET_STATUS        4u          /* arg: only NX_O_APPEND and NX_O_NONBLOCK change */
#define NX_FD_GET_TYPE          5u          /* returns NX_FD_TYPE_* */

/* fd flags (belong to one fd) */
#define NX_FD_CLOEXEC           0x1u

/* fd.ctl NX_FD_GET_TYPE results */
#define NX_FD_TYPE_FILE         1u
#define NX_FD_TYPE_DIR          2u
#define NX_FD_TYPE_PIPE_READ    3u
#define NX_FD_TYPE_PIPE_WRITE   4u
#define NX_FD_TYPE_PTY_MASTER   5u
#define NX_FD_TYPE_PTY_SLAVE    6u
#define NX_FD_TYPE_LOG          7u
#define NX_FD_TYPE_NULL         8u

/* stat_t.st_mode type bits (stat 25, fstat 26) */
#define NX_S_IFMT               0170000u
#define NX_S_IFIFO              0010000u    /* pipe */
#define NX_S_IFCHR              0020000u    /* PTY end, log, null */
#define NX_S_IFDIR              0040000u
#define NX_S_IFREG              0100000u

/* statfs (37) */
#define NX_STATFS_READONLY      0x1u
#define NX_STATFS_FREE_UNKNOWN  0xFFFFFFFFFFFFFFFFULL

typedef struct {
    uint32_t size;            /* in: bytes the caller has; out: bytes the kernel filled */
    uint32_t version;         /* 1 */
    uint32_t block_size;      /* bytes */
    uint32_t flags;           /* NX_STATFS_* */
    uint64_t total_blocks;
    uint64_t free_blocks;     /* NX_STATFS_FREE_UNKNOWN when the filesystem cannot tell */
    char     fs_type[16];     /* "fat32", "nxfs", "ramfs", NUL-terminated */
    char     mount_path[64];  /* NUL-terminated */
} nx_statfs_t;

#define NX_STATFS_V1_SIZE       112u

_Static_assert(sizeof(nx_statfs_t) == NX_STATFS_V1_SIZE, "nx_statfs_t v1 layout is 112 bytes");

/* mmap (11) / munmap (12), section 5 */
#define NX_PROT_READ            0x1u
#define NX_PROT_WRITE           0x2u        /* a writable page is also readable on x86 */
#define NX_MAP_ANON             0x1u        /* required: anonymous private memory */

/* pipe (16), section 6.2 */
#define NX_PIPE_BUF             4096u       /* buffer size; writes up to this size are atomic */

#endif /* NEOLYX_NX_FILE_H */
