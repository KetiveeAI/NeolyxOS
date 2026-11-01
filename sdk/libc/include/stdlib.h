/*
 * nxlibc - <stdlib.h>
 *
 * Memory: malloc and friends use nxlibc's own allocator on the process
 * heap (sys brk). free() of a pointer the allocator did not hand out, or a
 * second free() of the same block, is detected, logged to the kernel debug
 * log and ends the process with abort(): a corrupted heap is never used.
 *
 * Environment: getenv() reads the environment the kernel passes after argv.
 * Programs started with today's spawn get an empty one, so getenv returns
 * NULL until extended spawn (stream S3) passes variables.
 *
 * Not provided (no locale or wide-character support yet): mblen, mbtowc,
 * wctomb, mbstowcs, wcstombs.
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#ifndef NXLIBC_STDLIB_H
#define NXLIBC_STDLIB_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EXIT_SUCCESS    0
#define EXIT_FAILURE    1
#define RAND_MAX        0x7FFFFFFF
#define MB_CUR_MAX      ((size_t)1)

typedef struct { int quot; int rem; } div_t;
typedef struct { long quot; long rem; } ldiv_t;
typedef struct { long long quot; long long rem; } lldiv_t;

/* Numeric conversion */
double             atof(const char *s);
int                atoi(const char *s);
long               atol(const char *s);
long long          atoll(const char *s);
double             strtod(const char *restrict s, char **restrict end);
float              strtof(const char *restrict s, char **restrict end);
long double        strtold(const char *restrict s, char **restrict end);
long               strtol(const char *restrict s, char **restrict end, int base);
long long          strtoll(const char *restrict s, char **restrict end, int base);
unsigned long      strtoul(const char *restrict s, char **restrict end, int base);
unsigned long long strtoull(const char *restrict s, char **restrict end, int base);

/* Pseudo-random numbers (deterministic: the same seed gives the same sequence) */
int   rand(void);
void  srand(unsigned int seed);

/* Memory */
void *malloc(size_t size);
void *calloc(size_t count, size_t size);
void *realloc(void *ptr, size_t size);
void *aligned_alloc(size_t alignment, size_t size);
void  free(void *ptr);

/* Process */
_Noreturn void abort(void);
int   atexit(void (*fn)(void));
int   at_quick_exit(void (*fn)(void));
_Noreturn void exit(int status);
_Noreturn void _Exit(int status);
_Noreturn void quick_exit(int status);
char *getenv(const char *name);
int   system(const char *command);   /* no command processor: system(NULL) == 0 */

/* Searching and sorting */
void *bsearch(const void *key, const void *base, size_t count, size_t size,
              int (*compar)(const void *, const void *));
void  qsort(void *base, size_t count, size_t size,
            int (*compar)(const void *, const void *));

/* Integer arithmetic */
int       abs(int x);
long      labs(long x);
long long llabs(long long x);
div_t     div(int num, int den);
ldiv_t    ldiv(long num, long den);
lldiv_t   lldiv(long long num, long long den);

#ifdef __cplusplus
}
#endif

#endif /* NXLIBC_STDLIB_H */
