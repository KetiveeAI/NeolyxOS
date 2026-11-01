/*
 * nxlibc - <errno.h>
 *
 * Error numbers. The values are the kernel's (kernel/include/core/syscall.h):
 * a syscall that fails returns -E<name>, and the library stores E<name> in
 * errno. tests/host/libc_abi_check.c fails the build if the two drift.
 *
 * EDOM, EILSEQ and EOVERFLOW are produced by the library only (math,
 * conversions); the kernel never returns them.
 *
 * errno is per process (NeolyxOS has no user threads yet). It is a macro
 * over a function so that per-thread storage can be added later without
 * recompiling applications.
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#ifndef NXLIBC_ERRNO_H
#define NXLIBC_ERRNO_H

#ifdef __cplusplus
extern "C" {
#endif

#define EPERM           1   /* Operation not permitted */
#define ENOENT          2   /* No such file or directory */
#define ESRCH           3   /* No such process */
#define EINTR           4   /* Interrupted call */
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
#define EBUSY           16  /* Device or resource busy */
#define EEXIST          17  /* File exists */
#define EXDEV           18  /* Cross-device link */
#define ENODEV          19  /* No such device */
#define ENOTDIR         20  /* Not a directory */
#define EISDIR          21  /* Is a directory */
#define EINVAL          22  /* Invalid argument */
#define ENFILE          23  /* Too many open files in the system */
#define EMFILE          24  /* Too many open files in the process */
#define ENOTTY          25  /* Not a terminal */
#define EFBIG           27  /* File too large */
#define ENOSPC          28  /* No space left on device */
#define ESPIPE          29  /* Illegal seek */
#define EROFS           30  /* Read-only file system */
#define EPIPE           32  /* Broken pipe */
#define EDOM            33  /* Math argument out of domain (library only) */
#define ENAMETOOLONG    36  /* Name too long */
#define ENOSYS          38  /* Function not implemented */
#define ENOTEMPTY       39  /* Directory not empty */
#define ERANGE          40  /* Result not representable (kernel value) */
#define EOVERFLOW       75  /* Value too large for the type (library only) */
#define EILSEQ          84  /* Illegal byte sequence (library only) */
#define EMSGSIZE        90  /* Message too long / buffer too small */
/* Socket errors: networking proposal in the Beta ABI batch (nx_sock.h) */
#define EADDRINUSE      98  /* Address already in use */
#define ENETUNREACH     101 /* Network unreachable */
#define ECONNRESET      104 /* Connection reset by peer */
#define EISCONN         106 /* Socket already connected */
#define ENOTCONN        107 /* Socket not connected */
#define ETIMEDOUT       110 /* Timed out */
#define ECONNREFUSED    111 /* Connection refused */
#define EHOSTUNREACH    113 /* Host unreachable */

/* Highest error number strerror() knows by name */
#define NXC_ERRNO_MAX   113

int *__nxc_errno_location(void);
#define errno (*__nxc_errno_location())

#ifdef __cplusplus
}
#endif

#endif /* NXLIBC_ERRNO_H */
