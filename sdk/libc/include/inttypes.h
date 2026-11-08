/*
 * nxlibc - <inttypes.h>
 *
 * printf/scanf format macros for <stdint.h> types on x86_64 NeolyxOS
 * (LP64: int64_t and intptr_t are long), and the intmax_t helpers.
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#ifndef NXLIBC_INTTYPES_H
#define NXLIBC_INTTYPES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PRId8   "d"
#define PRId16  "d"
#define PRId32  "d"
#define PRId64  "ld"
#define PRIi8   "i"
#define PRIi16  "i"
#define PRIi32  "i"
#define PRIi64  "li"
#define PRIu8   "u"
#define PRIu16  "u"
#define PRIu32  "u"
#define PRIu64  "lu"
#define PRIo8   "o"
#define PRIo16  "o"
#define PRIo32  "o"
#define PRIo64  "lo"
#define PRIx8   "x"
#define PRIx16  "x"
#define PRIx32  "x"
#define PRIx64  "lx"
#define PRIX8   "X"
#define PRIX16  "X"
#define PRIX32  "X"
#define PRIX64  "lX"
#define PRIdMAX "jd"
#define PRIuMAX "ju"
#define PRIxMAX "jx"
#define PRIdPTR "ld"
#define PRIuPTR "lu"
#define PRIxPTR "lx"

#define SCNd8   "hhd"
#define SCNd16  "hd"
#define SCNd32  "d"
#define SCNd64  "ld"
#define SCNu8   "hhu"
#define SCNu16  "hu"
#define SCNu32  "u"
#define SCNu64  "lu"
#define SCNx8   "hhx"
#define SCNx16  "hx"
#define SCNx32  "x"
#define SCNx64  "lx"
#define SCNdMAX "jd"
#define SCNuMAX "ju"

typedef struct { intmax_t quot; intmax_t rem; } imaxdiv_t;

intmax_t  imaxabs(intmax_t x);
imaxdiv_t imaxdiv(intmax_t num, intmax_t den);
intmax_t  strtoimax(const char *restrict s, char **restrict end, int base);
uintmax_t strtoumax(const char *restrict s, char **restrict end, int base);

#ifdef __cplusplus
}
#endif

#endif /* NXLIBC_INTTYPES_H */
