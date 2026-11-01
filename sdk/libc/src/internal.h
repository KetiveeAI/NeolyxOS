/*
 * nxlibc - private declarations shared by the library's source files.
 * Not installed; applications never include this.
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#ifndef NXLIBC_INTERNAL_H
#define NXLIBC_INTERNAL_H

#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>

#define NXC_HIDDEN __attribute__((visibility("hidden")))

/* ---- start-up (src/start) ---- */
void     __nxc_init(int argc, char **argv, char **envp) NXC_HIDDEN;
extern char **__nxc_environ NXC_HIDDEN;
extern uint64_t __nxc_start_ms NXC_HIDDEN;  /* uptime when the program started */

/* ---- errors ---- */
/* Sets errno from a negative result of file syscall num (NXSYS_*) and
 * returns -1; returns rc unchanged when rc >= 0. */
long __nxc_file_ret(long rc, int num) NXC_HIDDEN;
/* Sets errno = -rc for a negative -errno result and returns -1 */
long __nxc_sys_ret(long rc) NXC_HIDDEN;

/* ---- diagnostics ---- */
/* Writes a NUL-terminated message to the kernel debug log, any length */
void __nxc_log(const char *msg) NXC_HIDDEN;
/* Writes "nxlibc: <what>\n" and an address in hex to the debug log */
void __nxc_log_ptr(const char *what, const void *p) NXC_HIDDEN;
/* Logs msg and ends the process with abort() */
_Noreturn void __nxc_fatal(const char *msg) NXC_HIDDEN;

/* ---- stdio ---- */
/* Output sink used by the formatter: a stream or a bounded buffer */
typedef struct nxc_sink {
    FILE   *file;       /* stream, or NULL for a buffer */
    char   *buf;        /* buffer (may be NULL when cap == 0) */
    size_t  cap;        /* buffer capacity, including the NUL */
    size_t  len;        /* bytes written to the buffer so far */
    size_t  total;      /* bytes the output has, written or not */
    int     error;      /* a stream write failed */
} nxc_sink_t;

void __nxc_sink_put(nxc_sink_t *s, const char *data, size_t n) NXC_HIDDEN;
/* Formats into the sink. Returns the total length, or -1 with errno set
 * (EOVERFLOW when the length does not fit an int, EINVAL for a bad format) */
int  __nxc_vformat(nxc_sink_t *s, const char *fmt, va_list ap) NXC_HIDDEN;

/* Floating-point conversion (src/stdio/printf_fp.c): writes the
 * conversion of v for spec 'f','F','e','E','g','G','a','A' with the given
 * flags into the sink. Returns 0 or -1 (errno set). */
#define NXC_FMT_LEFT   0x01
#define NXC_FMT_PLUS   0x02
#define NXC_FMT_SPACE  0x04
#define NXC_FMT_ALT    0x08
#define NXC_FMT_ZERO   0x10
int __nxc_format_fp(nxc_sink_t *s, double v, int spec, int flags, int width, int prec) NXC_HIDDEN;
void __nxc_pad(nxc_sink_t *s, char c, int n) NXC_HIDDEN;

/* Stream internals shared by file.c, printf.c and scanf.c */
size_t __nxc_stream_write(FILE *f, const void *data, size_t n) NXC_HIDDEN;
int    __nxc_vfprintf_locked(FILE *f, const char *fmt, va_list ap) NXC_HIDDEN;
int    __nxc_stream_getc(FILE *f) NXC_HIDDEN;
int    __nxc_stream_ungetc(int c, FILE *f) NXC_HIDDEN;

void __nxc_stdio_init(void) NXC_HIDDEN;
int  __nxc_stderr_is_log(void) NXC_HIDDEN;   /* stderr writes to the debug log */
void __nxc_stdio_flush_all(void) NXC_HIDDEN;

/* ---- stdlib ---- */
void __nxc_run_atexit(void) NXC_HIDDEN;

/* ---- math: shared extended-precision kernels (src/math) ---- */
long double __nxc_expl(long double x) NXC_HIDDEN;   /* e^x, |x| <= 11400 */
long double __nxc_logl(long double x) NXC_HIDDEN;   /* ln x, x > 0 finite */
long double __nxc_powl(double x, double y) NXC_HIDDEN; /* x^y, x > 0 finite, y finite */
/* Reduces x to r in [-pi/4, pi/4] with x = r + q*pi/2; returns q mod 4 */
int __nxc_rem_pio2(double x, long double *r) NXC_HIDDEN;
long double __nxc_sin_kernel(long double r) NXC_HIDDEN;  /* |r| <= pi/4 */
long double __nxc_cos_kernel(long double r) NXC_HIDDEN;  /* |r| <= pi/4 */
long double __nxc_atan_kernel(long double x) NXC_HIDDEN; /* any finite x */

/* ---- strings ---- */
/* Bounded copy that always NUL-terminates when cap > 0; returns strlen(src) */
size_t __nxc_strlcpy(char *dst, const char *src, size_t cap) NXC_HIDDEN;

#endif /* NXLIBC_INTERNAL_H */
