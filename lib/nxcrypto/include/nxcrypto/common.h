/*
 * nxcrypto - common definitions
 *
 * Status codes and the constant-time helpers every nxcrypto module uses.
 * The library is freestanding: it needs only <stddef.h> and <stdint.h>
 * from the compiler, never allocates, and never logs. Callers log failures
 * (a crypto library cannot know what is worth an audit record).
 *
 * As with any freestanding C, the compiler may emit calls to memcpy,
 * memset and memmove for struct copies; the kernel and every NeolyxOS
 * runtime provide them.
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#ifndef NXCRYPTO_COMMON_H
#define NXCRYPTO_COMMON_H

#include <stddef.h>
#include <stdint.h>

/*
 * Every function that can fail returns one of these. 0 is success,
 * failures are negative, so "if (rc != NXC_OK)" and "if (rc < 0)" both work.
 */
typedef enum {
    NXC_OK            =  0,
    NXC_ERR_ARG       = -1,  /* NULL pointer, unknown algorithm, bad length */
    NXC_ERR_STATE     = -2,  /* context used out of order (update after final) */
    NXC_ERR_LENGTH    = -3,  /* output buffer too small or input too long */
    NXC_ERR_VERIFY    = -4,  /* signature or tag did not verify */
    NXC_ERR_ENCODING  = -5,  /* malformed key, point or encoding */
    NXC_ERR_WEAK      = -6   /* result is a low-order / all-zero value */
} nxc_status_t;

/* Short, constant text for a status code (for the caller's log line). */
const char *nxc_status_str(int status);

/*
 * Overwrite len bytes at p with zeros in a way the compiler cannot remove.
 * Use it on every key, nonce state and intermediate secret before the
 * memory is reused or returned. p may be NULL only when len is 0.
 */
void nxc_memzero(void *p, size_t len);

/*
 * Compare two buffers in time that depends only on len, never on the
 * contents. Returns 1 if equal, 0 if not. Use it for every MAC, tag or
 * signature comparison.
 */
int nxc_ct_equal(const void *a, const void *b, size_t len);

#endif /* NXCRYPTO_COMMON_H */
