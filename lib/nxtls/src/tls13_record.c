/*
 * nxtls - TLS 1.3 record protection (RFC 8446 section 5.2 and 5.3):
 * per-record nonce from the IV and a 64-bit sequence number, AEAD with
 * the record header as additional data, and the inner content type with
 * zero padding.
 *
 * Checked against the RFC 8448 example handshake by
 * tests/host/crypto_tls13_trace_test.c.
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include "tls13.h"

#define TAG_LEN NXC_AEAD_TAG_LEN

static int is_chacha(const nxt__suite *s) {
    return s->id == NXT_TLS_CHACHA20_POLY1305_SHA256;
}

int nxt__rprot_init(nxt__rprot *p, const nxt__suite *suite, const uint8_t *traffic_secret) {
    if (p == NULL || suite == NULL || traffic_secret == NULL) return NXT_TLS_ERR_ARG;
    nxt__rprot_wipe(p);
    p->suite = suite;
    int rc = nxt__hkdf_expand_label(suite->hash, traffic_secret, suite->hash_len, "key",
                                    NULL, 0, p->key, suite->key_len);
    if (rc == NXT_TLS_OK)
        rc = nxt__hkdf_expand_label(suite->hash, traffic_secret, suite->hash_len, "iv",
                                    NULL, 0, p->iv, NXT__IV_LEN);
    if (rc == NXT_TLS_OK && !is_chacha(suite)) {
        if (nxc_gcm_init(&p->gcm, p->key, suite->key_len) != NXC_OK) rc = NXT_TLS_ERR_CRYPTO;
        /* The expanded AES key schedule is all GCM needs. */
        nxc_memzero(p->key, sizeof(p->key));
    }
    if (rc != NXT_TLS_OK) {
        nxt__rprot_wipe(p);
        return rc;
    }
    p->seq = 0;
    p->ready = 1;
    return NXT_TLS_OK;
}

void nxt__rprot_wipe(nxt__rprot *p) {
    if (p == NULL) return;
    nxc_gcm_wipe(&p->gcm);
    nxc_memzero(p, sizeof(*p));
}

/* nonce = iv XOR (64-bit big-endian seq, left-padded to the IV length) */
static void make_nonce(const nxt__rprot *p, uint8_t nonce[NXT__IV_LEN]) {
    for (size_t i = 0; i < NXT__IV_LEN; i++) nonce[i] = p->iv[i];
    for (size_t i = 0; i < 8; i++)
        nonce[NXT__IV_LEN - 1 - i] ^= (uint8_t)(p->seq >> (8 * i));
}

static int aead_seal(const nxt__rprot *p, uint8_t *buf, size_t len, const uint8_t aad[5]) {
    uint8_t nonce[NXT__IV_LEN];
    make_nonce(p, nonce);
    int rc = is_chacha(p->suite)
        ? nxc_chacha20poly1305_seal(buf, buf + len, p->key, nonce, aad, 5, buf, len)
        : nxc_gcm_seal(&p->gcm, buf, buf + len, nonce, aad, 5, buf, len);
    return rc == NXC_OK ? NXT_TLS_OK : NXT_TLS_ERR_CRYPTO;
}

static int aead_open(const nxt__rprot *p, uint8_t *buf, size_t len, const uint8_t aad[5]) {
    uint8_t nonce[NXT__IV_LEN];
    make_nonce(p, nonce);
    int rc = is_chacha(p->suite)
        ? nxc_chacha20poly1305_open(buf, p->key, nonce, aad, 5, buf, len, buf + len)
        : nxc_gcm_open(&p->gcm, buf, nonce, aad, 5, buf, len, buf + len);
    return rc == NXC_OK ? NXT_TLS_OK : NXT_TLS_ERR_BAD_RECORD_MAC;
}

int nxt__rprot_seal(nxt__rprot *p, uint8_t type, const uint8_t *data, size_t len,
                    size_t pad, uint8_t *out, size_t out_cap, size_t *out_len) {
    if (p == NULL || !p->ready || out == NULL || out_len == NULL || (data == NULL && len != 0))
        return NXT_TLS_ERR_ARG;
    if (type == 0 || type == NXT_TLS_CT_CHANGE_CIPHER_SPEC) return NXT_TLS_ERR_ARG;
    if (len > NXT_TLS_MAX_PLAINTEXT || pad > NXT_TLS_MAX_PLAINTEXT - len)
        return NXT_TLS_ERR_RECORD_OVERFLOW;
    if (p->seq == UINT64_MAX) return NXT_TLS_ERR_SEQ_EXHAUSTED;

    size_t inner = len + 1 + pad;                /* content || type || zeros */
    size_t total = NXT_TLS_RECORD_HEADER + inner + TAG_LEN;
    if (out_cap < total) return NXT_TLS_ERR_BUFFER;

    uint8_t *hdr = out, *body = out + NXT_TLS_RECORD_HEADER;
    hdr[0] = NXT_TLS_CT_APPLICATION_DATA;        /* opaque_type */
    hdr[1] = 0x03;                               /* legacy_record_version 0x0303 */
    hdr[2] = 0x03;
    hdr[3] = (uint8_t)((inner + TAG_LEN) >> 8);
    hdr[4] = (uint8_t)(inner + TAG_LEN);

    /* data is either already in place at body or does not overlap out. */
    if (data != NULL && data != body)
        for (size_t i = 0; i < len; i++) body[i] = data[i];
    body[len] = type;
    for (size_t i = 0; i < pad; i++) body[len + 1 + i] = 0;

    int rc = aead_seal(p, body, inner, hdr);
    if (rc != NXT_TLS_OK) {
        nxc_memzero(out, total);
        return rc;
    }
    p->seq++;
    *out_len = total;
    return NXT_TLS_OK;
}

int nxt__rprot_open(nxt__rprot *p, uint8_t *rec, size_t rec_len,
                    uint8_t *type, size_t *len) {
    if (p == NULL || !p->ready || rec == NULL || type == NULL || len == NULL)
        return NXT_TLS_ERR_ARG;
    if (rec_len < NXT_TLS_RECORD_HEADER) return NXT_TLS_ERR_ARG;
    size_t body_len = rec_len - NXT_TLS_RECORD_HEADER;
    if (((size_t)rec[3] << 8 | rec[4]) != body_len) return NXT_TLS_ERR_ARG;
    /* Protected records always have the opaque type application_data. */
    if (rec[0] != NXT_TLS_CT_APPLICATION_DATA) return NXT_TLS_ERR_UNEXPECTED_MESSAGE;
    if (body_len > NXT_TLS_MAX_CIPHERTEXT) return NXT_TLS_ERR_RECORD_OVERFLOW;
    /* At least the inner content type byte and the tag. Shorter cannot
     * authenticate (RFC 8446 5.2: bad_record_mac). */
    if (body_len < 1 + TAG_LEN) return NXT_TLS_ERR_BAD_RECORD_MAC;
    if (p->seq == UINT64_MAX) return NXT_TLS_ERR_SEQ_EXHAUSTED;

    uint8_t *body = rec + NXT_TLS_RECORD_HEADER;
    size_t inner = body_len - TAG_LEN;
    int rc = aead_open(p, body, inner, rec);
    if (rc != NXT_TLS_OK) {
        nxc_memzero(body, body_len);
        return rc;
    }
    p->seq++;

    /* Strip the zero padding; the last non-zero byte is the content type. */
    size_t i = inner;
    while (i > 0 && body[i - 1] == 0) i--;
    if (i == 0) {
        /* All zeros: no content type (RFC 8446 5.4: unexpected_message). */
        nxc_memzero(body, body_len);
        return NXT_TLS_ERR_UNEXPECTED_MESSAGE;
    }
    if (i - 1 > NXT_TLS_MAX_PLAINTEXT) {
        nxc_memzero(body, body_len);
        return NXT_TLS_ERR_RECORD_OVERFLOW;
    }
    *type = body[i - 1];
    *len = i - 1;
    return NXT_TLS_OK;
}

const char *nxt_tls_err_str(int err) {
    switch (err) {
    case NXT_TLS_OK: return "ok";
    case NXT_TLS_ERR_ARG: return "bad argument or wrong state";
    case NXT_TLS_ERR_BUFFER: return "buffer too small";
    case NXT_TLS_ERR_RECORD_OVERFLOW: return "record too long";
    case NXT_TLS_ERR_BAD_RECORD_MAC: return "record failed authentication";
    case NXT_TLS_ERR_SEQ_EXHAUSTED: return "record sequence number exhausted";
    case NXT_TLS_ERR_UNEXPECTED_MESSAGE: return "unexpected message";
    case NXT_TLS_ERR_CRYPTO: return "internal crypto failure";
    default: return "unknown error";
    }
}
