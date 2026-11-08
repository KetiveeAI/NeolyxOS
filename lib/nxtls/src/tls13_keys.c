/*
 * nxtls - TLS 1.3 key schedule (RFC 8446 section 7.1) for a full
 * handshake without a pre-shared key, plus the Finished MAC (4.4.4) and
 * the KeyUpdate step (7.2).
 *
 * Checked against the RFC 8448 example handshake by
 * tests/host/crypto_tls13_trace_test.c.
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include "tls13.h"

static const nxt__suite SUITES[] = {
    { NXT_TLS_AES_128_GCM_SHA256,       NXC_HASH_SHA256, 32, 16 },
    { NXT_TLS_AES_256_GCM_SHA384,       NXC_HASH_SHA384, 48, 32 },
    { NXT_TLS_CHACHA20_POLY1305_SHA256, NXC_HASH_SHA256, 32, 32 },
};

const nxt__suite *nxt__suite_find(uint16_t id) {
    for (size_t i = 0; i < sizeof(SUITES) / sizeof(SUITES[0]); i++)
        if (SUITES[i].id == id) return &SUITES[i];
    return NULL;
}

#define TLS13_PREFIX     "tls13 "
#define TLS13_PREFIX_LEN 6u
#define MAX_LABEL        (255u - TLS13_PREFIX_LEN)

int nxt__hkdf_expand_label(nxc_hash_alg_t alg, const uint8_t *secret, size_t secret_len,
                           const char *label, const uint8_t *context, size_t context_len,
                           uint8_t *out, size_t out_len) {
    if (secret == NULL || label == NULL || out == NULL || (context == NULL && context_len != 0))
        return NXT_TLS_ERR_ARG;
    size_t hlen = nxc_hash_digest_len(alg);
    if (hlen == 0 || secret_len != hlen || context_len > 255 || out_len == 0 ||
        out_len > 255 * hlen || out_len > 0xffffu)
        return NXT_TLS_ERR_ARG;

    size_t label_len = 0;
    while (label[label_len] != '\0') {
        if (++label_len > MAX_LABEL) return NXT_TLS_ERR_ARG;
    }

    /* struct { uint16 length; opaque label<7..255>; opaque context<0..255>; } HkdfLabel */
    uint8_t info[2 + 1 + 255 + 1 + 255];
    size_t n = 0;
    info[n++] = (uint8_t)(out_len >> 8);
    info[n++] = (uint8_t)out_len;
    info[n++] = (uint8_t)(TLS13_PREFIX_LEN + label_len);
    for (size_t i = 0; i < TLS13_PREFIX_LEN; i++) info[n++] = (uint8_t)TLS13_PREFIX[i];
    for (size_t i = 0; i < label_len; i++) info[n++] = (uint8_t)label[i];
    info[n++] = (uint8_t)context_len;
    for (size_t i = 0; i < context_len; i++) info[n++] = context[i];

    int rc = nxc_hkdf_expand(alg, secret, secret_len, info, n, out, out_len);
    nxc_memzero(info, sizeof(info));   /* the context can be a transcript hash */
    return rc == NXC_OK ? NXT_TLS_OK : NXT_TLS_ERR_CRYPTO;
}

/* Derive-Secret(secret, label, "") as used for "derived": the context is Hash(""). */
static int derive_empty(const nxt__suite *s, const uint8_t *secret, const char *label,
                        uint8_t *out) {
    uint8_t empty_hash[NXT__MAX_HASH];
    if (nxc_hash(s->hash, NULL, 0, empty_hash, sizeof(empty_hash)) != NXC_OK)
        return NXT_TLS_ERR_CRYPTO;
    return nxt__hkdf_expand_label(s->hash, secret, s->hash_len, label,
                                  empty_hash, s->hash_len, out, s->hash_len);
}

int nxt__ks_init(nxt__keysched *ks, const nxt__suite *suite) {
    if (ks == NULL || suite == NULL) return NXT_TLS_ERR_ARG;
    nxc_memzero(ks, sizeof(*ks));
    ks->suite = suite;

    /* Early Secret = HKDF-Extract(salt = 0, IKM = 0): no PSK. */
    uint8_t zeros[NXT__MAX_HASH] = {0};
    if (nxc_hkdf_extract(suite->hash, zeros, suite->hash_len, zeros, suite->hash_len,
                         ks->secret, sizeof(ks->secret)) != NXC_OK)
        return NXT_TLS_ERR_CRYPTO;
    ks->stage = 1;
    return NXT_TLS_OK;
}

int nxt__ks_handshake(nxt__keysched *ks, const uint8_t *shared, size_t shared_len,
                      const uint8_t *th) {
    if (ks == NULL || ks->stage != 1 || shared == NULL || shared_len == 0 || th == NULL)
        return NXT_TLS_ERR_ARG;
    const nxt__suite *s = ks->suite;
    uint8_t derived[NXT__MAX_HASH];
    int rc = derive_empty(s, ks->secret, "derived", derived);
    if (rc == NXT_TLS_OK) {
        /* Handshake Secret = HKDF-Extract(derived, (EC)DHE) */
        rc = nxc_hkdf_extract(s->hash, derived, s->hash_len, shared, shared_len,
                              ks->secret, sizeof(ks->secret)) == NXC_OK
                 ? NXT_TLS_OK : NXT_TLS_ERR_CRYPTO;
    }
    if (rc == NXT_TLS_OK)
        rc = nxt__hkdf_expand_label(s->hash, ks->secret, s->hash_len, "c hs traffic",
                                    th, s->hash_len, ks->client_hs, s->hash_len);
    if (rc == NXT_TLS_OK)
        rc = nxt__hkdf_expand_label(s->hash, ks->secret, s->hash_len, "s hs traffic",
                                    th, s->hash_len, ks->server_hs, s->hash_len);
    nxc_memzero(derived, sizeof(derived));
    if (rc != NXT_TLS_OK) {
        nxt__ks_wipe(ks);
        return rc;
    }
    ks->stage = 2;
    return NXT_TLS_OK;
}

int nxt__ks_application(nxt__keysched *ks, const uint8_t *th) {
    if (ks == NULL || ks->stage != 2 || th == NULL) return NXT_TLS_ERR_ARG;
    const nxt__suite *s = ks->suite;
    uint8_t derived[NXT__MAX_HASH];
    uint8_t zeros[NXT__MAX_HASH] = {0};
    int rc = derive_empty(s, ks->secret, "derived", derived);
    if (rc == NXT_TLS_OK) {
        /* Master Secret = HKDF-Extract(derived, 0) */
        rc = nxc_hkdf_extract(s->hash, derived, s->hash_len, zeros, s->hash_len,
                              ks->secret, sizeof(ks->secret)) == NXC_OK
                 ? NXT_TLS_OK : NXT_TLS_ERR_CRYPTO;
    }
    if (rc == NXT_TLS_OK)
        rc = nxt__hkdf_expand_label(s->hash, ks->secret, s->hash_len, "c ap traffic",
                                    th, s->hash_len, ks->client_ap, s->hash_len);
    if (rc == NXT_TLS_OK)
        rc = nxt__hkdf_expand_label(s->hash, ks->secret, s->hash_len, "s ap traffic",
                                    th, s->hash_len, ks->server_ap, s->hash_len);
    nxc_memzero(derived, sizeof(derived));
    if (rc != NXT_TLS_OK) {
        nxt__ks_wipe(ks);
        return rc;
    }
    /* The handshake traffic secrets are no longer needed. */
    nxc_memzero(ks->client_hs, sizeof(ks->client_hs));
    nxc_memzero(ks->server_hs, sizeof(ks->server_hs));
    ks->stage = 3;
    return NXT_TLS_OK;
}

void nxt__ks_wipe(nxt__keysched *ks) {
    if (ks != NULL) nxc_memzero(ks, sizeof(*ks));
}

int nxt__finished_mac(const nxt__suite *suite, const uint8_t *base_secret,
                      const uint8_t *th, uint8_t *out) {
    if (suite == NULL || base_secret == NULL || th == NULL || out == NULL)
        return NXT_TLS_ERR_ARG;
    uint8_t finished_key[NXT__MAX_HASH];
    int rc = nxt__hkdf_expand_label(suite->hash, base_secret, suite->hash_len, "finished",
                                    NULL, 0, finished_key, suite->hash_len);
    if (rc == NXT_TLS_OK &&
        nxc_hmac(suite->hash, finished_key, suite->hash_len, th, suite->hash_len,
                 out, suite->hash_len) != NXC_OK)
        rc = NXT_TLS_ERR_CRYPTO;
    nxc_memzero(finished_key, sizeof(finished_key));
    return rc;
}

int nxt__next_traffic_secret(const nxt__suite *suite, uint8_t *secret) {
    if (suite == NULL || secret == NULL) return NXT_TLS_ERR_ARG;
    uint8_t next[NXT__MAX_HASH];
    int rc = nxt__hkdf_expand_label(suite->hash, secret, suite->hash_len, "traffic upd",
                                    NULL, 0, next, suite->hash_len);
    if (rc == NXT_TLS_OK)
        for (size_t i = 0; i < suite->hash_len; i++) secret[i] = next[i];
    nxc_memzero(next, sizeof(next));
    return rc;
}
