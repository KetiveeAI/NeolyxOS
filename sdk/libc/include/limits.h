/*
 * nxlibc - <limits.h>
 *
 * Sizes of integer types for x86_64 NeolyxOS (LP64), taken from the
 * compiler's predefined macros so they always match the code it emits.
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#ifndef NXLIBC_LIMITS_H
#define NXLIBC_LIMITS_H

#define CHAR_BIT    8

#define SCHAR_MAX   __SCHAR_MAX__
#define SCHAR_MIN   (-SCHAR_MAX - 1)
#define UCHAR_MAX   (SCHAR_MAX * 2 + 1)

#ifdef __CHAR_UNSIGNED__
#define CHAR_MIN    0
#define CHAR_MAX    UCHAR_MAX
#else
#define CHAR_MIN    SCHAR_MIN
#define CHAR_MAX    SCHAR_MAX
#endif

/* No multibyte locales yet; 4 leaves room for UTF-8 without an ABI change */
#define MB_LEN_MAX  4

#define SHRT_MAX    __SHRT_MAX__
#define SHRT_MIN    (-SHRT_MAX - 1)
#define USHRT_MAX   (SHRT_MAX * 2 + 1)

#define INT_MAX     __INT_MAX__
#define INT_MIN     (-INT_MAX - 1)
#define UINT_MAX    (INT_MAX * 2U + 1U)

#define LONG_MAX    __LONG_MAX__
#define LONG_MIN    (-LONG_MAX - 1L)
#define ULONG_MAX   (LONG_MAX * 2UL + 1UL)

#define LLONG_MAX   __LONG_LONG_MAX__
#define LLONG_MIN   (-LLONG_MAX - 1LL)
#define ULLONG_MAX  (LLONG_MAX * 2ULL + 1ULL)

/* Longest path the kernel accepts in one call, including the NUL */
#define NXC_PATH_MAX 256

#endif /* NXLIBC_LIMITS_H */
