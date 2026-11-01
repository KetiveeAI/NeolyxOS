/*
 * nxtls internal: the TLS 1.3 key schedule (RFC 8446 section 7.1) and
 * record protection (section 5.2). Not part of the public API; the TLS
 * client and the host tests use it.
 *
 * Every function that takes a secret wipes its own temporaries, and the
 * structures here must be wiped with their *_wipe function before the
 * memory is reused.
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#ifndef NXTLS_SRC_TLS13_H
#define NXTLS_SRC_TLS13_H

#include "nxtls/tls.h"
#include "nxcrypto.h"

#define NXT__MAX_HASH 48u          /* SHA-384, the longest suite hash */
#define NXT__MAX_KEY  32u
#define NXT__IV_LEN   12u

/* A cipher suite: its hash and the AEAD key length. */
typedef struct {
    uint16_t id;
    nxc_hash_alg_t hash;
    uint8_t hash_len;
    uint8_t key_len;
} nxt__suite;

/* The suite for a TLS cipher suite id, or NULL if nxtls does not support it. */
const nxt__suite *nxt__suite_find(uint16_t id);

/*
 * HKDF-Expand-Label(secret, label, context, out_len) (RFC 8446 7.1).
 * label is given without the "tls13 " prefix. secret_len must be the
 * hash length; context may be NULL when context_len is 0.
 */
int nxt__hkdf_expand_label(nxc_hash_alg_t alg, const uint8_t *secret, size_t secret_len,
                           const char *label, const uint8_t *context, size_t context_len,
                           uint8_t *out, size_t out_len);

/*
 * Key schedule without a PSK (full handshake). The stages must be called
 * in order: nxt__ks_init, nxt__ks_handshake, nxt__ks_application.
 * Transcript hashes are passed in already computed, hash_len bytes each.
 */
typedef struct {
    const nxt__suite *suite;
    int stage;                         /* 0 none, 1 early, 2 handshake, 3 application */
    uint8_t secret[NXT__MAX_HASH];     /* Early, then Handshake, then Master Secret */
    uint8_t client_hs[NXT__MAX_HASH];  /* client_handshake_traffic_secret */
    uint8_t server_hs[NXT__MAX_HASH];  /* server_handshake_traffic_secret */
    uint8_t client_ap[NXT__MAX_HASH];  /* client_application_traffic_secret_0 */
    uint8_t server_ap[NXT__MAX_HASH];  /* server_application_traffic_secret_0 */
} nxt__keysched;

int  nxt__ks_init(nxt__keysched *ks, const nxt__suite *suite);
/* shared is the (EC)DHE secret; th is Transcript-Hash(ClientHello..ServerHello). */
int  nxt__ks_handshake(nxt__keysched *ks, const uint8_t *shared, size_t shared_len,
                       const uint8_t *th);
/* th is Transcript-Hash(ClientHello..server Finished). */
int  nxt__ks_application(nxt__keysched *ks, const uint8_t *th);
void nxt__ks_wipe(nxt__keysched *ks);

/*
 * Finished verify_data (RFC 8446 4.4.4): HMAC(finished_key, th) with
 * finished_key = HKDF-Expand-Label(base_secret, "finished", "", Hash.length).
 * out gets suite->hash_len bytes.
 */
int nxt__finished_mac(const nxt__suite *suite, const uint8_t *base_secret,
                      const uint8_t *th, uint8_t *out);

/* KeyUpdate (RFC 8446 7.2): secret = HKDF-Expand-Label(secret, "traffic upd", "", Hash.length). */
int nxt__next_traffic_secret(const nxt__suite *suite, uint8_t *secret);

/*
 * Record protection for one direction. Each traffic secret gets its own
 * nxt__rprot; the sequence number starts at 0 and is never reused.
 */
typedef struct {
    const nxt__suite *suite;
    nxc_gcm_ctx gcm;                   /* AES-GCM suites */
    uint8_t key[NXT__MAX_KEY];         /* ChaCha20-Poly1305 */
    uint8_t iv[NXT__IV_LEN];
    uint64_t seq;
    int ready;
} nxt__rprot;

/* Derive the key and IV from a traffic secret (RFC 8446 7.3) and reset seq to 0. */
int  nxt__rprot_init(nxt__rprot *p, const nxt__suite *suite, const uint8_t *traffic_secret);
void nxt__rprot_wipe(nxt__rprot *p);

/*
 * Protect one record: TLSInnerPlaintext = data || type || pad zero bytes,
 * sent as an application_data record. Writes the 5-byte header and the
 * ciphertext to out and the total length to *out_len. len + pad must not
 * exceed NXT_TLS_MAX_PLAINTEXT. data may be NULL when len is 0, may be
 * out + NXT_TLS_RECORD_HEADER (built in place), and must not otherwise
 * overlap out.
 */
int nxt__rprot_seal(nxt__rprot *p, uint8_t type, const uint8_t *data, size_t len,
                    size_t pad, uint8_t *out, size_t out_cap, size_t *out_len);

/*
 * Unprotect one record in place. rec holds the 5-byte header and the
 * payload (rec_len bytes in all, as read from the wire). On success the
 * plaintext is at rec + NXT_TLS_RECORD_HEADER, *len is its length and
 * *type is the real content type. On failure the payload is wiped.
 */
int nxt__rprot_open(nxt__rprot *p, uint8_t *rec, size_t rec_len,
                    uint8_t *type, size_t *len);

#endif /* NXTLS_SRC_TLS13_H */
