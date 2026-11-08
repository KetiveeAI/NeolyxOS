/*
 * nxlibc - <string.h>
 *
 * ISO C11 string and memory functions, plus the C23 additions memccpy,
 * strdup and strndup, plus two nxlibc extensions that make bounded code
 * easier to write: strnlen and strtok_r.
 *
 * Passing a NULL pointer where ISO C requires a string or object is
 * undefined behaviour; nxlibc does not hide it (it faults at the address),
 * except where a function documents otherwise.
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#ifndef NXLIBC_STRING_H
#define NXLIBC_STRING_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void  *memcpy(void *restrict dst, const void *restrict src, size_t n);
void  *memmove(void *dst, const void *src, size_t n);
void  *memset(void *dst, int c, size_t n);
int    memcmp(const void *a, const void *b, size_t n);
void  *memchr(const void *s, int c, size_t n);
void  *memccpy(void *restrict dst, const void *restrict src, int c, size_t n);

char  *strcpy(char *restrict dst, const char *restrict src);
char  *strncpy(char *restrict dst, const char *restrict src, size_t n);
char  *strcat(char *restrict dst, const char *restrict src);
char  *strncat(char *restrict dst, const char *restrict src, size_t n);

int    strcmp(const char *a, const char *b);
int    strncmp(const char *a, const char *b, size_t n);
int    strcoll(const char *a, const char *b);          /* "C" locale: strcmp */
size_t strxfrm(char *restrict dst, const char *restrict src, size_t n);

char  *strchr(const char *s, int c);
char  *strrchr(const char *s, int c);
size_t strspn(const char *s, const char *accept);
size_t strcspn(const char *s, const char *reject);
char  *strpbrk(const char *s, const char *accept);
char  *strstr(const char *haystack, const char *needle);
char  *strtok(char *restrict s, const char *restrict delim);

size_t strlen(const char *s);
char  *strerror(int errnum);

/* C23: copies are allocated with malloc(); NULL and errno = ENOMEM on failure */
char  *strdup(const char *s);
char  *strndup(const char *s, size_t n);

/* nxlibc extensions */
size_t strnlen(const char *s, size_t maxlen);   /* length, at most maxlen */
char  *strtok_r(char *restrict s, const char *restrict delim, char **restrict save);

#ifdef __cplusplus
}
#endif

#endif /* NXLIBC_STRING_H */
