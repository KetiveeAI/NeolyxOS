/*
 * nxlibc - errno storage, strerror and perror.
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include <errno.h>
#include <string.h>
#include <stdio.h>
#include "../internal.h"

static int g_errno;

int *__nxc_errno_location(void) {
    return &g_errno;
}

static const char *const g_messages[NXC_ERRNO_MAX + 1] = {
    [0]            = "No error",
    [EPERM]        = "Operation not permitted",
    [ENOENT]       = "No such file or directory",
    [ESRCH]        = "No such process",
    [EINTR]        = "Interrupted call",
    [EIO]          = "I/O error",
    [ENXIO]        = "No such device or address",
    [E2BIG]        = "Argument list too long",
    [ENOEXEC]      = "Exec format error",
    [EBADF]        = "Bad file descriptor",
    [ECHILD]       = "No child processes",
    [EAGAIN]       = "Resource temporarily unavailable",
    [ENOMEM]       = "Out of memory",
    [EACCES]       = "Permission denied",
    [EFAULT]       = "Bad address",
    [EBUSY]        = "Device or resource busy",
    [EEXIST]       = "File exists",
    [EXDEV]        = "Cross-device link",
    [ENODEV]       = "No such device",
    [ENOTDIR]      = "Not a directory",
    [EISDIR]       = "Is a directory",
    [EINVAL]       = "Invalid argument",
    [ENFILE]       = "Too many open files in the system",
    [EMFILE]       = "Too many open files",
    [ENOTTY]       = "Not a terminal",
    [EFBIG]        = "File too large",
    [ENOSPC]       = "No space left on device",
    [ESPIPE]       = "Illegal seek",
    [EROFS]        = "Read-only file system",
    [EPIPE]        = "Broken pipe",
    [EDOM]         = "Argument out of domain",
    [ENAMETOOLONG] = "File name too long",
    [ENOSYS]       = "Function not implemented",
    [ENOTEMPTY]    = "Directory not empty",
    [ERANGE]       = "Result out of range",
    [EOVERFLOW]    = "Value too large for data type",
    [EILSEQ]       = "Illegal byte sequence",
    [EMSGSIZE]     = "Message too long",
    [EADDRINUSE]   = "Address already in use",
    [ENETUNREACH]  = "Network unreachable",
    [ECONNRESET]   = "Connection reset by peer",
    [EISCONN]      = "Socket is already connected",
    [ENOTCONN]     = "Socket is not connected",
    [ETIMEDOUT]    = "Timed out",
    [ECONNREFUSED] = "Connection refused",
    [EHOSTUNREACH] = "Host unreachable",
};

/* Unknown numbers get "Unknown error N"; the buffer is static, like the
 * strings above, and is only written for unknown numbers. */
static char g_unknown[32];

char *strerror(int errnum) {
    if (errnum >= 0 && errnum <= NXC_ERRNO_MAX && g_messages[errnum]) {
        return (char *)g_messages[errnum];
    }
    char digits[12];
    int n = 0;
    unsigned int v = (errnum < 0) ? 0u - (unsigned int)errnum : (unsigned int)errnum;
    do {
        digits[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v && n < (int)sizeof(digits));
    size_t pos = __nxc_strlcpy(g_unknown, "Unknown error ", sizeof(g_unknown));
    if (errnum < 0 && pos < sizeof(g_unknown) - 1) g_unknown[pos++] = '-';
    while (n > 0 && pos < sizeof(g_unknown) - 1) g_unknown[pos++] = digits[--n];
    g_unknown[pos] = '\0';
    return g_unknown;
}

void perror(const char *s) {
    /* Read errno before any output can change it */
    const char *msg = strerror(errno);
    if (s && *s) {
        fputs(s, stderr);
        fputs(": ", stderr);
    }
    fputs(msg, stderr);
    fputc('\n', stderr);
}
