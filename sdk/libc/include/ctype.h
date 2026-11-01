/*
 * nxlibc - <ctype.h>
 *
 * Character classes of the "C" locale (ASCII), the only locale NeolyxOS
 * has. Every function accepts EOF and any value of unsigned char; bytes
 * 128-255 belong to no class. Other arguments are undefined in ISO C; here
 * they return 0 (or are returned unchanged by tolower/toupper) instead of
 * reading outside the class table.
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#ifndef NXLIBC_CTYPE_H
#define NXLIBC_CTYPE_H

#ifdef __cplusplus
extern "C" {
#endif

int isalnum(int c);
int isalpha(int c);
int isblank(int c);
int iscntrl(int c);
int isdigit(int c);
int isgraph(int c);
int islower(int c);
int isprint(int c);
int ispunct(int c);
int isspace(int c);
int isupper(int c);
int isxdigit(int c);
int tolower(int c);
int toupper(int c);

#ifdef __cplusplus
}
#endif

#endif /* NXLIBC_CTYPE_H */
