/*
 * nxcrypto - algorithm-generic hash front end over SHA-256/384/512.
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include "nxcrypto/sha2.h"
#include "internal.h"

size_t nxc_hash_digest_len(nxc_hash_alg_t alg) {
    switch (alg) {
    case NXC_HASH_SHA256: return NXC_SHA256_DIGEST_LEN;
    case NXC_HASH_SHA384: return NXC_SHA384_DIGEST_LEN;
    case NXC_HASH_SHA512: return NXC_SHA512_DIGEST_LEN;
    default:              return 0;
    }
}

size_t nxc_hash_block_len(nxc_hash_alg_t alg) {
    switch (alg) {
    case NXC_HASH_SHA256: return NXC_SHA256_BLOCK_LEN;
    case NXC_HASH_SHA384:
    case NXC_HASH_SHA512: return NXC_SHA512_BLOCK_LEN;
    default:              return 0;
    }
}

int nxc_hash_init(nxc_hash_ctx *ctx, nxc_hash_alg_t alg) {
    if (ctx == NULL) return NXC_ERR_ARG;
    ctx->alg = alg;
    switch (alg) {
    case NXC_HASH_SHA256: return nxc_sha256_init(&ctx->u.s256);
    case NXC_HASH_SHA384: return nxc_sha384_init(&ctx->u.s512);
    case NXC_HASH_SHA512: return nxc_sha512_init(&ctx->u.s512);
    default:
        ctx->alg = NXC_HASH_NONE;
        return NXC_ERR_ARG;
    }
}

int nxc_hash_update(nxc_hash_ctx *ctx, const void *data, size_t len) {
    if (ctx == NULL) return NXC_ERR_ARG;
    switch (ctx->alg) {
    case NXC_HASH_SHA256: return nxc_sha256_update(&ctx->u.s256, data, len);
    case NXC_HASH_SHA384:
    case NXC_HASH_SHA512: return nxc_sha512_update(&ctx->u.s512, data, len);
    default:              return NXC_ERR_STATE;
    }
}

int nxc_hash_final(nxc_hash_ctx *ctx, uint8_t *out, size_t out_cap) {
    if (ctx == NULL || out == NULL) return NXC_ERR_ARG;
    size_t dlen = nxc_hash_digest_len(ctx->alg);
    if (dlen == 0) return NXC_ERR_STATE;
    if (out_cap < dlen) return NXC_ERR_LENGTH;
    int rc;
    if (ctx->alg == NXC_HASH_SHA256) rc = nxc_sha256_final(&ctx->u.s256, out);
    else rc = nxc_sha512_final(&ctx->u.s512, out);
    ctx->alg = NXC_HASH_NONE;
    return rc;
}

int nxc_hash(nxc_hash_alg_t alg, const void *data, size_t len,
             uint8_t *out, size_t out_cap) {
    nxc_hash_ctx ctx;
    int rc = nxc_hash_init(&ctx, alg);
    if (rc == NXC_OK) rc = nxc_hash_update(&ctx, data, len);
    if (rc == NXC_OK) rc = nxc_hash_final(&ctx, out, out_cap);
    nxc_memzero(&ctx, sizeof(ctx));
    return rc;
}
