/*
 * nxcrypto - signature verification for certificates and TLS:
 * ECDSA over NIST P-256 / P-384 (FIPS 186-4, SEC 1), and RSA
 * PKCS#1 v1.5 and PSS (RFC 8017).
 *
 * Verification only: every input (key, signature, hash) is public, so
 * these functions are allowed to run in data-dependent time. There is no
 * signing here; NeolyxOS signs with Ed25519 (curve25519.h).
 *
 * Hashes are passed already computed (SHA-256/384/512 from sha2.h).
 * Every function returns NXC_OK only for a valid signature. Anything else
 * (NXC_ERR_VERIFY, NXC_ERR_ENCODING, NXC_ERR_ARG, NXC_ERR_LENGTH) means
 * "not signed by this key".
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#ifndef NXCRYPTO_PUBKEY_H
#define NXCRYPTO_PUBKEY_H

#include "nxcrypto/common.h"
#include "nxcrypto/sha2.h"

typedef enum {
    NXC_EC_NONE = 0,
    NXC_EC_P256 = 1,    /* secp256r1 / prime256v1 */
    NXC_EC_P384 = 2     /* secp384r1 */
} nxc_ec_curve_t;

/*
 * ECDSA verify. pub is an uncompressed point 0x04 || X || Y (65 bytes
 * for P-256, 97 for P-384); it is checked to lie on the curve. r and s
 * are big-endian unsigned integers (leading zeros allowed) and must be in
 * [1, n-1]. The hash is truncated to the curve size as FIPS 186-4
 * section 6.4 requires.
 */
int nxc_ecdsa_verify(nxc_ec_curve_t curve,
                     const uint8_t *pub, size_t pub_len,
                     const uint8_t *hash, size_t hash_len,
                     const uint8_t *r, size_t r_len,
                     const uint8_t *s, size_t s_len);

/*
 * Same, with the signature as DER Ecdsa-Sig-Value
 * (SEQUENCE { INTEGER r, INTEGER s }), the form used by X.509 and TLS.
 * Only strict DER is accepted (minimal lengths and integers, no trailing
 * bytes).
 */
int nxc_ecdsa_verify_der(nxc_ec_curve_t curve,
                         const uint8_t *pub, size_t pub_len,
                         const uint8_t *hash, size_t hash_len,
                         const uint8_t *sig, size_t sig_len);

/* Moduli from 1024 to 4096 bits are accepted here; certificate and TLS
 * policy (nxtls) requires at least 2048. The exponent must be odd and
 * at least 3. sig_len must equal the modulus length in bytes. */
#define NXC_RSA_MIN_BITS 1024u
#define NXC_RSA_MAX_BITS 4096u

/* RSASSA-PKCS1-v1_5 (RFC 8017 section 8.2.2), DigestInfo for alg. */
int nxc_rsa_verify_pkcs1v15(const uint8_t *n, size_t n_len,
                            const uint8_t *e, size_t e_len,
                            nxc_hash_alg_t alg,
                            const uint8_t *hash, size_t hash_len,
                            const uint8_t *sig, size_t sig_len);

/* RSASSA-PSS (RFC 8017 section 8.1.2) with MGF1 over the same hash and
 * an exact salt length (TLS 1.3 uses salt_len = hash length). */
int nxc_rsa_verify_pss(const uint8_t *n, size_t n_len,
                       const uint8_t *e, size_t e_len,
                       nxc_hash_alg_t alg,
                       const uint8_t *hash, size_t hash_len,
                       size_t salt_len,
                       const uint8_t *sig, size_t sig_len);

#endif /* NXCRYPTO_PUBKEY_H */
