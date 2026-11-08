/*
 * nxcrypto - HMAC (RFC 2104, FIPS 198-1) and HKDF (RFC 5869).
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include "nxcrypto/hmac.h"
#include "internal.h"

void nxc_hmac_wipe(nxc_hmac_ctx *ctx) {
    if (ctx != NULL) nxc_memzero(ctx, sizeof(*ctx));
}

int nxc_hmac_init(nxc_hmac_ctx *ctx, nxc_hash_alg_t alg,
                  const void *key, size_t key_len) {
    if (ctx == NULL || (key == NULL && key_len != 0)) return NXC_ERR_ARG;
    size_t bl = nxc_hash_block_len(alg);
    size_t dl = nxc_hash_digest_len(alg);
    if (bl == 0 || dl == 0) return NXC_ERR_ARG;

    /* K0: the key, hashed first if longer than a block, zero padded. */
    uint8_t k0[NXC_HASH_MAX_BLOCK_LEN];
    uint8_t pad[NXC_HASH_MAX_BLOCK_LEN];
    for (size_t i = 0; i < bl; i++) k0[i] = 0;
    int rc = NXC_OK;
    if (key_len > bl) {
        rc = nxc_hash(alg, key, key_len, k0, sizeof(k0));
    } else if (key_len > 0) {
        nxc_copy(k0, key, key_len);
    }

    if (rc == NXC_OK) {
        for (size_t i = 0; i < bl; i++) pad[i] = (uint8_t)(k0[i] ^ 0x36);
        rc = nxc_hash_init(&ctx->inner, alg);
        if (rc == NXC_OK) rc = nxc_hash_update(&ctx->inner, pad, bl);
    }
    if (rc == NXC_OK) {
        for (size_t i = 0; i < bl; i++) pad[i] = (uint8_t)(k0[i] ^ 0x5c);
        rc = nxc_hash_init(&ctx->outer, alg);
        if (rc == NXC_OK) rc = nxc_hash_update(&ctx->outer, pad, bl);
    }

    nxc_memzero(k0, sizeof(k0));
    nxc_memzero(pad, sizeof(pad));
    if (rc != NXC_OK) nxc_hmac_wipe(ctx);
    return rc;
}

int nxc_hmac_update(nxc_hmac_ctx *ctx, const void *data, size_t len) {
    if (ctx == NULL) return NXC_ERR_ARG;
    return nxc_hash_update(&ctx->inner, data, len);
}

int nxc_hmac_final(nxc_hmac_ctx *ctx, uint8_t *out, size_t out_cap) {
    if (ctx == NULL || out == NULL) return NXC_ERR_ARG;
    size_t dl = nxc_hash_digest_len(ctx->inner.alg);
    if (dl == 0 || ctx->outer.alg != ctx->inner.alg) return NXC_ERR_STATE;
    if (out_cap < dl) return NXC_ERR_LENGTH;

    uint8_t ih[NXC_HASH_MAX_DIGEST_LEN];
    int rc = nxc_hash_final(&ctx->inner, ih, sizeof(ih));
    if (rc == NXC_OK) rc = nxc_hash_update(&ctx->outer, ih, dl);
    if (rc == NXC_OK) rc = nxc_hash_final(&ctx->outer, out, out_cap);
    nxc_memzero(ih, sizeof(ih));
    nxc_hmac_wipe(ctx);
    return rc;
}

int nxc_hmac(nxc_hash_alg_t alg, const void *key, size_t key_len,
             const void *data, size_t len, uint8_t *out, size_t out_cap) {
    nxc_hmac_ctx ctx;
    int rc = nxc_hmac_init(&ctx, alg, key, key_len);
    if (rc == NXC_OK) rc = nxc_hmac_update(&ctx, data, len);
    if (rc == NXC_OK) rc = nxc_hmac_final(&ctx, out, out_cap);
    nxc_hmac_wipe(&ctx);
    return rc;
}

int nxc_hkdf_extract(nxc_hash_alg_t alg,
                     const void *salt, size_t salt_len,
                     const void *ikm, size_t ikm_len,
                     uint8_t *prk, size_t prk_cap) {
    size_t dl = nxc_hash_digest_len(alg);
    if (dl == 0 || prk == NULL) return NXC_ERR_ARG;
    if ((salt == NULL && salt_len != 0) || (ikm == NULL && ikm_len != 0))
        return NXC_ERR_ARG;
    if (prk_cap < dl) return NXC_ERR_LENGTH;

    uint8_t zeros[NXC_HASH_MAX_DIGEST_LEN];
    if (salt_len == 0) {
        for (size_t i = 0; i < dl; i++) zeros[i] = 0;
        salt = zeros;
        salt_len = dl;
    }
    return nxc_hmac(alg, salt, salt_len, ikm, ikm_len, prk, prk_cap);
}

int nxc_hkdf_expand(nxc_hash_alg_t alg,
                    const uint8_t *prk, size_t prk_len,
                    const void *info, size_t info_len,
                    uint8_t *okm, size_t okm_len) {
    size_t dl = nxc_hash_digest_len(alg);
    if (dl == 0 || prk == NULL || (info == NULL && info_len != 0) ||
        (okm == NULL && okm_len != 0))
        return NXC_ERR_ARG;
    if (prk_len < dl || okm_len > 255 * dl) return NXC_ERR_LENGTH;

    /* T(i) = HMAC(PRK, T(i-1) | info | i), OKM = T(1) | T(2) | ... */
    uint8_t t[NXC_HASH_MAX_DIGEST_LEN];
    size_t t_len = 0, done = 0;
    uint8_t counter = 1;
    int rc = NXC_OK;
    while (done < okm_len) {
        nxc_hmac_ctx ctx;
        rc = nxc_hmac_init(&ctx, alg, prk, prk_len);
        if (rc == NXC_OK) rc = nxc_hmac_update(&ctx, t, t_len);
        if (rc == NXC_OK) rc = nxc_hmac_update(&ctx, info, info_len);
        if (rc == NXC_OK) rc = nxc_hmac_update(&ctx, &counter, 1);
        if (rc == NXC_OK) rc = nxc_hmac_final(&ctx, t, sizeof(t));
        nxc_hmac_wipe(&ctx);
        if (rc != NXC_OK) break;
        t_len = dl;
        size_t take = okm_len - done < dl ? okm_len - done : dl;
        nxc_copy(okm + done, t, take);
        done += take;
        counter++;
    }
    nxc_memzero(t, sizeof(t));
    if (rc != NXC_OK && okm != NULL) nxc_memzero(okm, okm_len);
    return rc;
}
