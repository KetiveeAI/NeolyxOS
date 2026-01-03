/*
 * NeolyxOS Crypto Header
 * 
 * Cryptographic primitives for security modules.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef NEOLYX_CRYPTO_H
#define NEOLYX_CRYPTO_H

#include <stdint.h>

/* Hash functions */
typedef struct {
    uint32_t state[8];
    uint32_t count[2];
    uint8_t  buffer[64];
} sha256_ctx_t;

void sha256_init(sha256_ctx_t *ctx);
void sha256_update(sha256_ctx_t *ctx, const uint8_t *data, uint32_t len);
void sha256_final(sha256_ctx_t *ctx, uint8_t hash[32]);

/* AES-256 (Stub for now) */
/* Context size depends on implementation */
typedef struct {
    uint32_t rk[60];
} aes256_ctx_t;

void aes256_init(aes256_ctx_t *ctx, const uint8_t *key);
void aes256_encrypt(aes256_ctx_t *ctx, const uint8_t *in, uint8_t *out);
void aes256_decrypt(aes256_ctx_t *ctx, const uint8_t *in, uint8_t *out);

/* Random Number Generation */
void crypto_srand(uint32_t seed);
uint32_t crypto_rand(void);
void crypto_rand_bytes(uint8_t *buf, uint32_t len);

#endif /* NEOLYX_CRYPTO_H */
