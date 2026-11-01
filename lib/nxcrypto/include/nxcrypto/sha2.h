/*
 * nxcrypto - SHA-256, SHA-384 and SHA-512 (FIPS 180-4)
 *
 * Streaming:  init, update (any number of times), final.
 * One shot:   nxc_sha256(), nxc_sha384(), nxc_sha512().
 *
 * A context is a plain struct with no pointers: copying it (struct
 * assignment) forks the hash, which TLS uses for transcript hashes.
 * final() wipes the context; it must be init()ed again before reuse.
 * Messages are limited to 2^61 - 1 bytes (the bit length must fit in
 * 64 bits); a longer total returns NXC_ERR_LENGTH.
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#ifndef NXCRYPTO_SHA2_H
#define NXCRYPTO_SHA2_H

#include "nxcrypto/common.h"

#define NXC_SHA256_DIGEST_LEN  32u
#define NXC_SHA256_BLOCK_LEN   64u
#define NXC_SHA384_DIGEST_LEN  48u
#define NXC_SHA512_DIGEST_LEN  64u
#define NXC_SHA512_BLOCK_LEN  128u   /* also SHA-384 */

typedef struct {
    uint32_t state[8];
    uint64_t total;                     /* bytes hashed so far */
    uint8_t  buf[NXC_SHA256_BLOCK_LEN];
    uint32_t buf_len;
    uint32_t magic;                     /* set by init, cleared by final */
} nxc_sha256_ctx;

typedef struct {
    uint64_t state[8];
    uint64_t total;
    uint8_t  buf[NXC_SHA512_BLOCK_LEN];
    uint32_t buf_len;
    uint32_t digest_len;                /* 48 for SHA-384, 64 for SHA-512 */
    uint32_t magic;
} nxc_sha512_ctx;

int nxc_sha256_init(nxc_sha256_ctx *ctx);
int nxc_sha256_update(nxc_sha256_ctx *ctx, const void *data, size_t len);
int nxc_sha256_final(nxc_sha256_ctx *ctx, uint8_t out[NXC_SHA256_DIGEST_LEN]);
int nxc_sha256(const void *data, size_t len, uint8_t out[NXC_SHA256_DIGEST_LEN]);

/* SHA-384 and SHA-512 share nxc_sha512_ctx and nxc_sha512_update(). */
int nxc_sha384_init(nxc_sha512_ctx *ctx);
int nxc_sha512_init(nxc_sha512_ctx *ctx);
int nxc_sha512_update(nxc_sha512_ctx *ctx, const void *data, size_t len);
/* Writes ctx's digest length (48 or 64 bytes) to out. */
int nxc_sha512_final(nxc_sha512_ctx *ctx, uint8_t *out);
int nxc_sha384(const void *data, size_t len, uint8_t out[NXC_SHA384_DIGEST_LEN]);
int nxc_sha512(const void *data, size_t len, uint8_t out[NXC_SHA512_DIGEST_LEN]);

/* ------------------------------------------------------------------ */
/* Algorithm-generic hashing (used by HMAC, HKDF and TLS).             */
/* ------------------------------------------------------------------ */

typedef enum {
    NXC_HASH_NONE   = 0,
    NXC_HASH_SHA256 = 1,
    NXC_HASH_SHA384 = 2,
    NXC_HASH_SHA512 = 3
} nxc_hash_alg_t;

#define NXC_HASH_MAX_DIGEST_LEN NXC_SHA512_DIGEST_LEN
#define NXC_HASH_MAX_BLOCK_LEN  NXC_SHA512_BLOCK_LEN

typedef struct {
    nxc_hash_alg_t alg;
    union {
        nxc_sha256_ctx s256;
        nxc_sha512_ctx s512;
    } u;
} nxc_hash_ctx;

/* Digest / block length of alg in bytes, 0 for an unknown alg. */
size_t nxc_hash_digest_len(nxc_hash_alg_t alg);
size_t nxc_hash_block_len(nxc_hash_alg_t alg);

int nxc_hash_init(nxc_hash_ctx *ctx, nxc_hash_alg_t alg);
int nxc_hash_update(nxc_hash_ctx *ctx, const void *data, size_t len);
/* out_cap must be at least nxc_hash_digest_len(alg). */
int nxc_hash_final(nxc_hash_ctx *ctx, uint8_t *out, size_t out_cap);
int nxc_hash(nxc_hash_alg_t alg, const void *data, size_t len,
             uint8_t *out, size_t out_cap);

#endif /* NXCRYPTO_SHA2_H */
