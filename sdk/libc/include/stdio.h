/*
 * nxlibc - <stdio.h>
 *
 * Buffered streams over the kernel's file descriptors, and nxlibc's own
 * printf/scanf formatters.
 *
 * Standard streams. When the kernel gives a process standard descriptors
 * (ABI flag NXSYS_ABI_FLAG_FILEIO, stream S3's extended spawn), stdin,
 * stdout and stderr are descriptors 0, 1 and 2. Before that, or when a
 * program was started without them, stdout and stderr write to the kernel
 * debug log (the serial console) one line at a time, and stdin is at end of
 * file. A program never reads or writes a file it opened itself through a
 * standard stream by accident.
 *
 * Floating point: printf prints the exact decimal value of a double, rounded
 * to the requested precision with round-half-to-even. long double arguments
 * (L) are converted to double first.
 *
 * Not provided: tmpfile, tmpnam (no temporary-file area yet), and the
 * wide-character functions.
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#ifndef NXLIBC_STDIO_H
#define NXLIBC_STDIO_H

#include <stddef.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct __nxc_file FILE;
typedef struct { long long __off; } fpos_t;

#define EOF             (-1)
#define BUFSIZ          4096
#define FOPEN_MAX       16      /* the kernel's descriptor limit per process today */
#define FILENAME_MAX    256     /* longest path the kernel accepts, with the NUL */

#define _IOFBF          0       /* fully buffered */
#define _IOLBF          1       /* line buffered */
#define _IONBF          2       /* unbuffered */

#define SEEK_SET        0
#define SEEK_CUR        1
#define SEEK_END        2

extern FILE *const stdin;
extern FILE *const stdout;
extern FILE *const stderr;
/* ISO C requires these to be macros */
#define stdin  stdin
#define stdout stdout
#define stderr stderr

/* Files */
FILE *fopen(const char *restrict path, const char *restrict mode);
FILE *freopen(const char *restrict path, const char *restrict mode, FILE *restrict f);
int   fclose(FILE *f);
int   fflush(FILE *f);              /* fflush(NULL) flushes every output stream */
void  setbuf(FILE *restrict f, char *restrict buf);
int   setvbuf(FILE *restrict f, char *restrict buf, int mode, size_t size);
int   remove(const char *path);
int   rename(const char *oldpath, const char *newpath);

/* Formatted output */
int printf(const char *restrict fmt, ...) __attribute__((format(printf, 1, 2)));
int fprintf(FILE *restrict f, const char *restrict fmt, ...) __attribute__((format(printf, 2, 3)));
int sprintf(char *restrict buf, const char *restrict fmt, ...) __attribute__((format(printf, 2, 3)));
int snprintf(char *restrict buf, size_t size, const char *restrict fmt, ...)
    __attribute__((format(printf, 3, 4)));
int vprintf(const char *restrict fmt, va_list ap);
int vfprintf(FILE *restrict f, const char *restrict fmt, va_list ap);
int vsprintf(char *restrict buf, const char *restrict fmt, va_list ap);
int vsnprintf(char *restrict buf, size_t size, const char *restrict fmt, va_list ap);

/* Formatted input */
int scanf(const char *restrict fmt, ...) __attribute__((format(scanf, 1, 2)));
int fscanf(FILE *restrict f, const char *restrict fmt, ...) __attribute__((format(scanf, 2, 3)));
int sscanf(const char *restrict s, const char *restrict fmt, ...) __attribute__((format(scanf, 2, 3)));
int vscanf(const char *restrict fmt, va_list ap);
int vfscanf(FILE *restrict f, const char *restrict fmt, va_list ap);
int vsscanf(const char *restrict s, const char *restrict fmt, va_list ap);

/* Character input and output */
int   fgetc(FILE *f);
int   getc(FILE *f);
int   getchar(void);
char *fgets(char *restrict s, int n, FILE *restrict f);
int   ungetc(int c, FILE *f);
int   fputc(int c, FILE *f);
int   putc(int c, FILE *f);
int   putchar(int c);
int   fputs(const char *restrict s, FILE *restrict f);
int   puts(const char *s);

/* Direct input and output */
size_t fread(void *restrict buf, size_t size, size_t count, FILE *restrict f);
size_t fwrite(const void *restrict buf, size_t size, size_t count, FILE *restrict f);

/* Positioning */
int  fseek(FILE *f, long offset, int whence);
long ftell(FILE *f);
void rewind(FILE *f);
int  fgetpos(FILE *restrict f, fpos_t *restrict pos);
int  fsetpos(FILE *f, const fpos_t *pos);

/* Errors */
void clearerr(FILE *f);
int  feof(FILE *f);
int  ferror(FILE *f);
void perror(const char *s);

#ifdef __cplusplus
}
#endif

#endif /* NXLIBC_STDIO_H */
