/*
 * nxlibc - kernel ABI capability cache and file-syscall error translation.
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include <nx/sys.h>
#include <errno.h>
#include "../internal.h"

static int      g_abi_queried;
static uint16_t g_abi_flags;

uint16_t nxsys_abi_flags(void) {
    if (!g_abi_queried) {
        nxsys_abi_info_t info;
        long rc = nxsys_abi_query(&info);
        g_abi_flags = (rc == 0) ? info.flags : 0;
        g_abi_queried = 1;
        if (rc != 0) {
            __nxc_log("nxlibc: abi.query failed; assuming ABI 1.0 with no optional features\n");
        }
    }
    return g_abi_flags;
}

int nxsys_abi_has(uint16_t flag) {
    if (flag == 0) return 0;
    return (nxsys_abi_flags() & flag) == flag;
}

/*
 * Kernels before the FILEIO flag (stream S3) are not uniform about file
 * errors (kernel/src/core/syscall.c):
 *
 *   open, stat, chdir, getcwd, close   -errno only
 *   read, write, seek                  -EBADF (-9), -EFAULT (-14), or a
 *                                      VFS code from the VFS layer
 *   readdir, mkdir, rmdir, rename      -EFAULT (-14) or a VFS code
 *
 * VFS codes (kernel/include/fs/vfs.h):
 *   -1 NOT_FOUND  -2 PERM     -3 IO        -4 NO_MEM    -5 EXIST
 *   -6 NOT_DIR    -7 IS_DIR   -8 NOT_EMPTY -9 BUSY      -10 INVALID
 *   -11 NO_SPACE  -12 READ_ONLY
 * The read/write/seek VFS paths never return BUSY, so -9 there is EBADF.
 */
static int vfs_code_errno(long rc) {
    switch (rc) {
    case -1:  return ENOENT;
    case -2:  return EACCES;
    case -3:  return EIO;
    case -4:  return ENOMEM;
    case -5:  return EEXIST;
    case -6:  return ENOTDIR;
    case -7:  return EISDIR;
    case -8:  return ENOTEMPTY;
    case -9:  return EBUSY;
    case -10: return EINVAL;
    case -11: return ENOSPC;
    case -12: return EROFS;
    default:  return EIO;   /* a filesystem driver's own negative value */
    }
}

static int plain_errno(long rc) {
    if (rc < -4095) return EIO;
    return (int)-rc;
}

int nxsys_file_errno(long rc, int num) {
    if (rc >= 0) return 0;
    if (nxsys_abi_has(NXSYS_ABI_FLAG_FILEIO)) {
        return plain_errno(rc);         /* the kernel promises -errno */
    }
    switch (num) {
    case NXSYS_READ:
    case NXSYS_WRITE:
    case NXSYS_SEEK:
        if (rc == -EBADF || rc == -EFAULT) return (int)-rc;
        return vfs_code_errno(rc);
    case NXSYS_READDIR:
    case NXSYS_MKDIR:
    case NXSYS_RMDIR:
    case NXSYS_RENAME:
        if (rc == -EFAULT) return EFAULT;
        return vfs_code_errno(rc);
    default:
        /* open, stat, chdir, getcwd, close, and anything newer */
        return plain_errno(rc);
    }
}

long __nxc_file_ret(long rc, int num) {
    if (rc >= 0) return rc;
    errno = nxsys_file_errno(rc, num);
    return -1;
}

long __nxc_sys_ret(long rc) {
    if (rc >= 0) return rc;
    errno = (rc < -4095) ? EIO : (int)-rc;
    return -1;
}
