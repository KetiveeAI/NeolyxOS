/*
 * nxcrypto - RSA signature verification (RFC 8017): RSASSA-PKCS1-v1_5
 * (section 8.2.2) and RSASSA-PSS with MGF1 (sections 8.1.2, 9.1.2,
 * B.2.1). Verification only; all inputs are public (see bn.h).
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include "nxcrypto/pubkey.h"
#include "bn.h"
#include "internal.h"

#define RSA_MAX_BYTES (NXC_RSA_MAX_BITS / 8)

/*
 * em = sig^e mod n as k bytes (RSAVP1). Checks key and signature sizes.
 * *mod_bits receives the modulus length in bits.
 */
static int rsa_public(uint8_t *em, size_t *k_out, uint32_t *mod_bits,
                      const uint8_t *n, size_t n_len, const uint8_t *e, size_t e_len,
                      const uint8_t *sig, size_t sig_len) {
    if (n == NULL || e == NULL || sig == NULL || n_len == 0 || e_len == 0) return NXC_ERR_ARG;
    while (n_len > 0 && n[0] == 0) { n++; n_len--; }       /* DER may add a 0x00 */
    while (e_len > 0 && e[0] == 0) { e++; e_len--; }
    if (n_len == 0 || e_len == 0 || n_len > RSA_MAX_BYTES || e_len > n_len) return NXC_ERR_ENCODING;

    nxc_mont m;
    if (nxc_mont_init(&m, n, n_len) != 0) return NXC_ERR_ENCODING;   /* even or tiny */
    uint32_t bits = nxc_bn_bits(m.m, m.n);
    if (bits < NXC_RSA_MIN_BITS || bits > NXC_RSA_MAX_BITS) return NXC_ERR_LENGTH;
    if ((e[e_len - 1] & 1u) == 0 || (e_len == 1 && e[0] < 3)) return NXC_ERR_ENCODING;

    size_t k = n_len;
    if (sig_len != k) return NXC_ERR_VERIFY;                 /* RFC 8017 8.2.2 step 1 */
    uint64_t s[NXC_BN_MAX_LIMBS], sm[NXC_BN_MAX_LIMBS], r[NXC_BN_MAX_LIMBS];
    nxc_bn_from_be(s, m.n, sig, sig_len);
    if (nxc_bn_cmp(s, m.m, m.n) >= 0) return NXC_ERR_VERIFY;  /* s must be < n */

    nxc_mont_to(&m, sm, s);
    nxc_mont_exp(&m, r, sm, e, e_len);
    nxc_mont_from(&m, s, r);
    if (nxc_bn_to_be(em, k, s, m.n) != 0) return NXC_ERR_VERIFY;
    *k_out = k;
    *mod_bits = bits;
    return NXC_OK;
}

/* DER DigestInfo prefixes (RFC 8017 section 9.2, note 1). */
static const uint8_t DI_SHA256[19] = { 0x30, 0x31, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86, 0x48, 0x01,
                                       0x65, 0x03, 0x04, 0x02, 0x01, 0x05, 0x00, 0x04, 0x20 };
static const uint8_t DI_SHA384[19] = { 0x30, 0x41, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86, 0x48, 0x01,
                                       0x65, 0x03, 0x04, 0x02, 0x02, 0x05, 0x00, 0x04, 0x30 };
static const uint8_t DI_SHA512[19] = { 0x30, 0x51, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86, 0x48, 0x01,
                                       0x65, 0x03, 0x04, 0x02, 0x03, 0x05, 0x00, 0x04, 0x40 };

int nxc_rsa_verify_pkcs1v15(const uint8_t *n, size_t n_len,
                            const uint8_t *e, size_t e_len,
                            nxc_hash_alg_t alg,
                            const uint8_t *hash, size_t hash_len,
                            const uint8_t *sig, size_t sig_len) {
    const uint8_t *di;
    switch (alg) {
    case NXC_HASH_SHA256: di = DI_SHA256; break;
    case NXC_HASH_SHA384: di = DI_SHA384; break;
    case NXC_HASH_SHA512: di = DI_SHA512; break;
    default: return NXC_ERR_ARG;
    }
    if (hash == NULL || hash_len != nxc_hash_digest_len(alg)) return NXC_ERR_ARG;

    uint8_t em[RSA_MAX_BYTES], want[RSA_MAX_BYTES];
    size_t k;
    uint32_t bits;
    int rc = rsa_public(em, &k, &bits, n, n_len, e, e_len, sig, sig_len);
    if (rc != NXC_OK) return rc;

    /* EM' = 0x00 0x01 FF..FF 0x00 DigestInfo hash, with at least 8 FF. */
    size_t t_len = sizeof(DI_SHA256) + hash_len;
    if (k < t_len + 11) return NXC_ERR_VERIFY;
    want[0] = 0x00;
    want[1] = 0x01;
    for (size_t i = 2; i < k - t_len - 1; i++) want[i] = 0xff;
    want[k - t_len - 1] = 0x00;
    nxc_copy(want + k - t_len, di, sizeof(DI_SHA256));
    nxc_copy(want + k - hash_len, hash, hash_len);
    /* Compare the whole encoding: no parsing of the attacker's padding. */
    return nxc_ct_equal(em, want, k) ? NXC_OK : NXC_ERR_VERIFY;
}

/* mask ^= MGF1(seed)[0..len) (RFC 8017 appendix B.2.1). */
static int mgf1_xor(nxc_hash_alg_t alg, uint8_t *mask, size_t len,
                    const uint8_t *seed, size_t seed_len) {
    size_t hl = nxc_hash_digest_len(alg);
    uint8_t blk[NXC_HASH_MAX_DIGEST_LEN], ctr[4];
    for (uint32_t c = 0; (size_t)c * hl < len; c++) {
        nxc_hash_ctx h;
        nxc_store32_be(ctr, c);
        int rc = nxc_hash_init(&h, alg);
        if (rc == NXC_OK) rc = nxc_hash_update(&h, seed, seed_len);
        if (rc == NXC_OK) rc = nxc_hash_update(&h, ctr, 4);
        if (rc == NXC_OK) rc = nxc_hash_final(&h, blk, sizeof(blk));
        if (rc != NXC_OK) return rc;
        for (size_t i = 0; i < hl && (size_t)c * hl + i < len; i++) mask[(size_t)c * hl + i] ^= blk[i];
    }
    return NXC_OK;
}

int nxc_rsa_verify_pss(const uint8_t *n, size_t n_len,
                       const uint8_t *e, size_t e_len,
                       nxc_hash_alg_t alg,
                       const uint8_t *hash, size_t hash_len,
                       size_t salt_len,
                       const uint8_t *sig, size_t sig_len) {
    size_t hl = nxc_hash_digest_len(alg);
    if (hl == 0 || hash == NULL || hash_len != hl) return NXC_ERR_ARG;

    uint8_t buf[RSA_MAX_BYTES];
    size_t k;
    uint32_t bits;
    int rc = rsa_public(buf, &k, &bits, n, n_len, e, e_len, sig, sig_len);
    if (rc != NXC_OK) return rc;

    /* EMSA-PSS-VERIFY with emBits = modBits - 1. */
    uint32_t em_bits = bits - 1;
    size_t em_len = (em_bits + 7) / 8;
    const uint8_t *em = buf + (k - em_len);      /* k - em_len is 0 or 1 */
    if (k > em_len && buf[0] != 0) return NXC_ERR_VERIFY;
    if (em_len < hl + salt_len + 2) return NXC_ERR_VERIFY;
    if (em[em_len - 1] != 0xbc) return NXC_ERR_VERIFY;

    size_t db_len = em_len - hl - 1;
    uint8_t db[RSA_MAX_BYTES];
    nxc_copy(db, em, db_len);
    const uint8_t *h = em + db_len;
    uint8_t top_mask = (uint8_t)(0xffu >> (8 * em_len - em_bits));
    if (db[0] & (uint8_t)~top_mask) return NXC_ERR_VERIFY;
    rc = mgf1_xor(alg, db, db_len, h, hl);
    if (rc != NXC_OK) return rc;
    db[0] &= top_mask;

    size_t ps_len = db_len - salt_len - 1;
    for (size_t i = 0; i < ps_len; i++)
        if (db[i] != 0) return NXC_ERR_VERIFY;
    if (db[ps_len] != 0x01) return NXC_ERR_VERIFY;

    /* H' = Hash(0x00 * 8 || mHash || salt) */
    static const uint8_t zeros[8] = { 0 };
    uint8_t h2[NXC_HASH_MAX_DIGEST_LEN];
    nxc_hash_ctx hc;
    rc = nxc_hash_init(&hc, alg);
    if (rc == NXC_OK) rc = nxc_hash_update(&hc, zeros, 8);
    if (rc == NXC_OK) rc = nxc_hash_update(&hc, hash, hl);
    if (rc == NXC_OK) rc = nxc_hash_update(&hc, db + db_len - salt_len, salt_len);
    if (rc == NXC_OK) rc = nxc_hash_final(&hc, h2, sizeof(h2));
    if (rc != NXC_OK) return rc;
    return nxc_ct_equal(h, h2, hl) ? NXC_OK : NXC_ERR_VERIFY;
}
