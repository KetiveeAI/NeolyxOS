/*
 * nxlibc - <nx/libc.h>: nxlibc extensions
 *
 * Things a NeolyxOS program may want from its C library that ISO C has no
 * name for: the library version, heap statistics and an integrity check,
 * and printf to the kernel debug log.
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#ifndef NXLIBC_NX_LIBC_H
#define NXLIBC_NX_LIBC_H

#include <stddef.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NXLIBC_VERSION_MAJOR 0
#define NXLIBC_VERSION_MINOR 1
#define NXLIBC_VERSION_PATCH 0

/* "nxlibc 0.1.0" */
const char *nxc_version(void);

typedef struct {
    size_t heap_bytes;      /* bytes between the heap start and the break */
    size_t used_bytes;      /* bytes in allocated blocks, headers included */
    size_t free_bytes;      /* bytes in free blocks */
    size_t used_blocks;
    size_t free_blocks;
    size_t largest_free;    /* largest single free block, header included */
    size_t peak_heap_bytes; /* highest heap_bytes seen */
} nxc_heap_stats_t;

/* Fills *out. 0, or -1 (errno EINVAL) when out is NULL. */
int nxc_heap_stats(nxc_heap_stats_t *out);

/* Walks the whole heap and checks every header and free-list link.
 * 0 if consistent; otherwise logs the first problem and returns -1. */
int nxc_heap_check(void);

/* Returns heap memory above the last used block to the kernel. Bytes released. */
size_t nxc_heap_trim(void);

/* printf to the kernel debug log (serial console), whatever stdout is.
 * Output is cut into lines of at most 255 bytes. */
int nxc_debugf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
int nxc_vdebugf(const char *fmt, va_list ap);

#ifdef __cplusplus
}
#endif

#endif /* NXLIBC_NX_LIBC_H */
