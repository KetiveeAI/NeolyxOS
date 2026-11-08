/*
 * nxlibc - diagnostics to the kernel debug log.
 *
 * debug.print takes a NUL-terminated string and uses at most 255 bytes of
 * it, so longer messages are sent in pieces.
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include <nx/sys.h>
#include <nx/libc.h>
#include <stdlib.h>
#include <string.h>
#include "../internal.h"

#define LOG_CHUNK 255

void __nxc_log(const char *msg) {
    if (!msg) return;
    char piece[LOG_CHUNK + 1];
    size_t len = strlen(msg);
    while (len > 0) {
        size_t n = (len > LOG_CHUNK) ? LOG_CHUNK : len;
        memcpy(piece, msg, n);
        piece[n] = '\0';
        nxsys_debug_print(piece);
        msg += n;
        len -= n;
    }
}

void __nxc_log_ptr(const char *what, const void *p) {
    static const char hex[] = "0123456789abcdef";
    char line[160];
    /* Room for " 0x" + 16 digits + "\n" + NUL */
    const size_t tail = 21;
    __nxc_strlcpy(line, "nxlibc: ", sizeof(line) - tail);
    size_t pos = strlen(line);
    __nxc_strlcpy(line + pos, what ? what : "", sizeof(line) - tail - pos);
    pos = strlen(line);
    uintptr_t v = (uintptr_t)p;
    line[pos++] = ' ';
    line[pos++] = '0';
    line[pos++] = 'x';
    for (int shift = 60; shift >= 0; shift -= 4) line[pos++] = hex[(v >> shift) & 0xF];
    line[pos++] = '\n';
    line[pos] = '\0';
    __nxc_log(line);
}

_Noreturn void __nxc_fatal(const char *msg) {
    __nxc_log("nxlibc: fatal: ");
    __nxc_log(msg);
    __nxc_log("\n");
    abort();
}

const char *nxc_version(void) {
    return "nxlibc 0.1.0";
}
