/*
 * nxcrypto - X25519 key agreement (RFC 7748) and Ed25519 signatures
 * (RFC 8032, "pure" Ed25519 without context or prehash).
 *
 * Secret-dependent work (scalar multiplication, the ladder, scalar
 * arithmetic) runs in constant time: no branches or memory indexes that
 * depend on secret data.
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#ifndef NXCRYPTO_CURVE25519_H
#define NXCRYPTO_CURVE25519_H

#include "nxcrypto/common.h"

#define NXC_X25519_KEY_LEN        32u
#define NXC_ED25519_SEED_LEN      32u   /* the private key */
#define NXC_ED25519_PUBLIC_LEN    32u
#define NXC_ED25519_SIGNATURE_LEN 64u

/*
 * out = scalar * point (u-coordinates, little-endian, RFC 7748 section 5).
 * The scalar is clamped internally; the caller's copy is not changed.
 * Returns NXC_ERR_WEAK when the result is all zeros (the peer sent a
 * low-order point); TLS must then abort the handshake (RFC 8446 7.4.2).
 * out may alias neither input.
 */
int nxc_x25519(uint8_t out[NXC_X25519_KEY_LEN],
               const uint8_t scalar[NXC_X25519_KEY_LEN],
               const uint8_t point[NXC_X25519_KEY_LEN]);

/* out = scalar * 9 (the public key for a 32-byte random private key). */
int nxc_x25519_public(uint8_t out[NXC_X25519_KEY_LEN],
                      const uint8_t scalar[NXC_X25519_KEY_LEN]);

/* Public key from a 32-byte secret seed (RFC 8032 section 5.1.5). */
int nxc_ed25519_public(uint8_t pub[NXC_ED25519_PUBLIC_LEN],
                       const uint8_t seed[NXC_ED25519_SEED_LEN]);

/*
 * Sign msg with the secret seed (RFC 8032 section 5.1.6). Signing is
 * deterministic. The public key is recomputed from the seed rather than
 * taken from the caller, so a mismatched key pair can never leak the
 * secret. msg may be NULL only when msg_len is 0.
 */
int nxc_ed25519_sign(uint8_t sig[NXC_ED25519_SIGNATURE_LEN],
                     const void *msg, size_t msg_len,
                     const uint8_t seed[NXC_ED25519_SEED_LEN]);

/*
 * Verify (RFC 8032 section 5.1.7, cofactorless equation [S]B = R + [k]A).
 * Returns NXC_OK for a valid signature, NXC_ERR_VERIFY for an invalid
 * one, NXC_ERR_ENCODING for a malformed public key or S >= L.
 * Anything other than NXC_OK must be treated as "not signed".
 */
int nxc_ed25519_verify(const uint8_t sig[NXC_ED25519_SIGNATURE_LEN],
                       const void *msg, size_t msg_len,
                       const uint8_t pub[NXC_ED25519_PUBLIC_LEN]);

#endif /* NXCRYPTO_CURVE25519_H */
