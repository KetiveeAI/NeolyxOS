/*
 * nxcrypto - HMAC (RFC 2104 / FIPS 198-1) and HKDF (RFC 5869)
 *
 * Over SHA-256, SHA-384 or SHA-512. A context can be copied by struct
 * assignment. Contexts hold key material: final() wipes them, and a
 * caller that abandons a context early must call nxc_hmac_wipe().
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#ifndef NXCRYPTO_HMAC_H
#define NXCRYPTO_HMAC_H

#include "nxcrypto/sha2.h"

typedef struct {
    nxc_hash_ctx inner;
    nxc_hash_ctx outer;
} nxc_hmac_ctx;

/* key may be NULL only when key_len is 0. Any key length is allowed. */
int nxc_hmac_init(nxc_hmac_ctx *ctx, nxc_hash_alg_t alg,
                  const void *key, size_t key_len);
int nxc_hmac_update(nxc_hmac_ctx *ctx, const void *data, size_t len);
/* Writes the full digest; out_cap must be >= the digest length. */
int nxc_hmac_final(nxc_hmac_ctx *ctx, uint8_t *out, size_t out_cap);
void nxc_hmac_wipe(nxc_hmac_ctx *ctx);

int nxc_hmac(nxc_hash_alg_t alg, const void *key, size_t key_len,
             const void *data, size_t len, uint8_t *out, size_t out_cap);

/*
 * HKDF-Extract: prk = HMAC(salt, ikm). A NULL / empty salt means a
 * string of digest-length zero bytes (RFC 5869 section 2.2).
 * prk_cap must be >= the digest length; exactly that many bytes are
 * written.
 */
int nxc_hkdf_extract(nxc_hash_alg_t alg,
                     const void *salt, size_t salt_len,
                     const void *ikm, size_t ikm_len,
                     uint8_t *prk, size_t prk_cap);

/*
 * HKDF-Expand: fills okm[0..okm_len) from prk and info.
 * prk_len must be >= the digest length; okm_len <= 255 * digest length.
 */
int nxc_hkdf_expand(nxc_hash_alg_t alg,
                    const uint8_t *prk, size_t prk_len,
                    const void *info, size_t info_len,
                    uint8_t *okm, size_t okm_len);

#endif /* NXCRYPTO_HMAC_H */
