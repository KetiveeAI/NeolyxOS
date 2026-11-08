/*
 * nxcrypto - ChaCha20-Poly1305 AEAD (RFC 8439 section 2.8).
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include "nxcrypto/aead.h"
#include "internal.h"

void nxc__chacha20_poly_key(uint8_t otk[32], const uint8_t key[32], const uint8_t nonce[12]);

/* Tag over aad | pad16 | ct | pad16 | le64(aad_len) | le64(ct_len). */
static int aead_tag(uint8_t tag[16], const uint8_t key[32], const uint8_t nonce[12],
                    const void *aad, size_t aad_len, const uint8_t *ct, size_t len) {
    static const uint8_t zeros[16] = { 0 };
    uint8_t otk[32], lens[16];
    nxc_poly1305_ctx p;
    nxc__chacha20_poly_key(otk, key, nonce);
    nxc_store64_le(lens, (uint64_t)aad_len);
    nxc_store64_le(lens + 8, (uint64_t)len);

    int rc = nxc_poly1305_init(&p, otk);
    if (rc == NXC_OK) rc = nxc_poly1305_update(&p, aad, aad_len);
    if (rc == NXC_OK) rc = nxc_poly1305_update(&p, zeros, (16 - aad_len % 16) % 16);
    if (rc == NXC_OK) rc = nxc_poly1305_update(&p, ct, len);
    if (rc == NXC_OK) rc = nxc_poly1305_update(&p, zeros, (16 - len % 16) % 16);
    if (rc == NXC_OK) rc = nxc_poly1305_update(&p, lens, 16);
    if (rc == NXC_OK) rc = nxc_poly1305_final(&p, tag);
    nxc_memzero(otk, sizeof(otk));
    nxc_memzero(&p, sizeof(p));
    return rc;
}

static int args_ok(const void *out, const void *key, const void *nonce,
                   const void *aad, size_t aad_len, const void *in, size_t len) {
    if (key == NULL || nonce == NULL) return 0;
    if (aad == NULL && aad_len != 0) return 0;
    if (len != 0 && (in == NULL || out == NULL)) return 0;
    /* RFC 8439 limit: 2^38 - 64 bytes per nonce (32-bit counter from 1). */
    if ((uint64_t)len > ((uint64_t)1 << 38) - 64) return 0;
    return 1;
}

int nxc_chacha20poly1305_seal(uint8_t *out, uint8_t tag[NXC_AEAD_TAG_LEN],
                              const uint8_t key[NXC_CHACHA20_KEY_LEN],
                              const uint8_t nonce[NXC_CHACHA20_NONCE_LEN],
                              const void *aad, size_t aad_len,
                              const uint8_t *in, size_t len) {
    if (tag == NULL || !args_ok(out, key, nonce, aad, aad_len, in, len)) return NXC_ERR_ARG;
    int rc = nxc_chacha20_xor(out, in, len, key, nonce, 1);
    if (rc == NXC_OK) rc = aead_tag(tag, key, nonce, aad, aad_len, out, len);
    if (rc != NXC_OK) {
        if (out != NULL) nxc_memzero(out, len);
        nxc_memzero(tag, NXC_AEAD_TAG_LEN);
    }
    return rc;
}

int nxc_chacha20poly1305_open(uint8_t *out,
                              const uint8_t key[NXC_CHACHA20_KEY_LEN],
                              const uint8_t nonce[NXC_CHACHA20_NONCE_LEN],
                              const void *aad, size_t aad_len,
                              const uint8_t *in, size_t len,
                              const uint8_t tag[NXC_AEAD_TAG_LEN]) {
    if (tag == NULL || !args_ok(out, key, nonce, aad, aad_len, in, len)) return NXC_ERR_ARG;
    uint8_t want[16];
    int rc = aead_tag(want, key, nonce, aad, aad_len, in, len);
    int vrc = NXC_OK;           /* verification result, kept out of rc's branches */
    if (rc == NXC_OK) {
        /* Decrypt, then keep the output only if the tag matched. The
         * match result never drives a branch, so a forgery costs the same
         * time as a valid record. */
        uint32_t ok = (uint32_t)nxc_ct_equal(want, tag, 16);
        rc = nxc_chacha20_xor(out, in, len, key, nonce, 1);
        if (rc == NXC_OK) vrc = nxc__aead_keep_if(out, len, ok);
    }
    if (rc != NXC_OK && out != NULL) nxc_memzero(out, len);
    nxc_memzero(want, sizeof(want));
    return rc != NXC_OK ? rc : vrc;
}
