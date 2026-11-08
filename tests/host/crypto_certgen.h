/*
 * Test certificates for the nxtls host tests, built with nxcrypto
 * Ed25519 and a few DER helpers. Nothing here is used outside tests.
 *
 * certgen_make() builds a v3 certificate shaped like a real web PKI
 * certificate: a CA has a critical basicConstraints (cA), a critical
 * keyUsage keyCertSign and a subjectKeyIdentifier; a server certificate
 * has subjectAltName dNSName, extKeyUsage serverAuth, a critical keyUsage
 * digitalSignature and an authorityKeyIdentifier. Key identifiers are the
 * first 20 bytes of the Ed25519 public key.
 *
 * Host-only (uses the C library). Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#ifndef CRYPTO_CERTGEN_H
#define CRYPTO_CERTGEN_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nxcrypto.h"

typedef struct { uint8_t b[2048]; size_t n; } dbuf;

static inline void put(dbuf *d, const void *p, size_t n) {
    if (d->n + n > sizeof(d->b)) { printf("ERROR: dbuf overflow\n"); exit(2); }
    memcpy(d->b + d->n, p, n);
    d->n += n;
}

/* Append tag, DER length and the contents of `in`. */
static inline void wrap(dbuf *out, uint8_t tag, const dbuf *in) {
    uint8_t h[4] = { tag };
    size_t hl;
    if (in->n < 0x80) { h[1] = (uint8_t)in->n; hl = 2; }
    else if (in->n < 0x100) { h[1] = 0x81; h[2] = (uint8_t)in->n; hl = 3; }
    else { h[1] = 0x82; h[2] = (uint8_t)(in->n >> 8); h[3] = (uint8_t)in->n; hl = 4; }
    put(out, h, hl);
    put(out, in->b, in->n);
}

static inline void wrap_bytes(dbuf *out, uint8_t tag, const void *p, size_t n) {
    dbuf t = { .n = 0 };
    put(&t, p, n);
    wrap(out, tag, &t);
}

static inline void extension(dbuf *exts, const uint8_t oid[3], int critical, const dbuf *value) {
    dbuf e = { .n = 0 };
    wrap_bytes(&e, 0x06, oid, 3);
    if (critical) put(&e, "\x01\x01\xff", 3);
    wrap(&e, 0x04, value);
    wrap(exts, 0x30, &e);
}

static inline void certgen_name(dbuf *name, const char *cn) {
    static const uint8_t cn_oid[] = { 0x55, 0x04, 0x03 };
    dbuf atv = { .n = 0 }, rdn = { .n = 0 }, rdns = { .n = 0 };
    wrap_bytes(&atv, 0x06, cn_oid, 3);
    wrap_bytes(&atv, 0x0c, cn, strlen(cn));
    wrap(&rdn, 0x30, &atv);
    wrap(&rdns, 0x31, &rdn);
    wrap(name, 0x30, &rdns);
}

typedef struct {
    const char *subject_cn, *issuer_cn;
    const uint8_t *subject_seed, *issuer_seed;   /* Ed25519 private keys (32 bytes) */
    int is_ca;
    const char *dns_name;                        /* server certificates: the SAN */
    const char *not_before, *not_after;          /* UTCTime "YYMMDDhhmmssZ" */
    uint8_t serial;                              /* 1..127 */
} certgen_spec;

/* Build the certificate into out; returns its length (exits on overflow). */
static inline size_t certgen_make(const certgen_spec *s, uint8_t *out, size_t cap) {
    static const uint8_t alg_ed25519[] = { 0x30, 0x05, 0x06, 0x03, 0x2b, 0x65, 0x70 };
    static const uint8_t san_oid[3] = { 0x55, 0x1d, 0x11 }, eku_oid[3] = { 0x55, 0x1d, 0x25 },
                         ku_oid[3] = { 0x55, 0x1d, 0x0f }, aki_oid[3] = { 0x55, 0x1d, 0x23 },
                         ski_oid[3] = { 0x55, 0x1d, 0x0e }, bc_oid[3] = { 0x55, 0x1d, 0x13 };
    static const uint8_t server_auth[] = { 0x2b, 0x06, 0x01, 0x05, 0x05, 0x07, 0x03, 0x01 };
    uint8_t pub[32], issuer_pub[32], sig[64];
    if (nxc_ed25519_public(pub, s->subject_seed) != NXC_OK ||
        nxc_ed25519_public(issuer_pub, s->issuer_seed) != NXC_OK) {
        printf("ERROR: certgen key\n");
        exit(2);
    }

    dbuf subject = { .n = 0 }, issuer = { .n = 0 };
    certgen_name(&subject, s->subject_cn);
    certgen_name(&issuer, s->issuer_cn);

    dbuf tbs = { .n = 0 }, t = { .n = 0 }, v = { .n = 0 }, spki = { .n = 0 }, bits = { .n = 0 };
    put(&tbs, "\xa0\x03\x02\x01\x02", 5);                     /* version v3 */
    uint8_t serial[3] = { 0x02, 0x01, s->serial };
    put(&tbs, serial, 3);
    put(&tbs, alg_ed25519, sizeof(alg_ed25519));
    put(&tbs, issuer.b, issuer.n);
    wrap_bytes(&v, 0x17, s->not_before, 13);
    wrap_bytes(&v, 0x17, s->not_after, 13);
    wrap(&tbs, 0x30, &v);
    put(&tbs, subject.b, subject.n);
    put(&spki, alg_ed25519, sizeof(alg_ed25519));
    put(&bits, "\x00", 1);
    put(&bits, pub, 32);
    wrap(&spki, 0x03, &bits);
    wrap(&tbs, 0x30, &spki);

    dbuf exts = { .n = 0 }, val = { .n = 0 }, inner = { .n = 0 }, all = { .n = 0 }, x3 = { .n = 0 };
    if (s->is_ca) {
        put(&val, "\x30\x03\x01\x01\xff", 5);                  /* cA TRUE */
        extension(&exts, bc_oid, 1, &val);
        val.n = 0;
        put(&val, "\x03\x02\x01\x06", 4);                      /* keyCertSign, cRLSign */
        extension(&exts, ku_oid, 1, &val);
        val.n = 0;
        wrap_bytes(&val, 0x04, pub, 20);
        extension(&exts, ski_oid, 0, &val);
    } else {
        wrap_bytes(&inner, 0x82, s->dns_name, strlen(s->dns_name));
        wrap(&val, 0x30, &inner);
        extension(&exts, san_oid, 0, &val);
        val.n = inner.n = 0;
        wrap_bytes(&inner, 0x06, server_auth, sizeof(server_auth));
        wrap(&val, 0x30, &inner);
        extension(&exts, eku_oid, 0, &val);
        val.n = 0;
        put(&val, "\x03\x02\x07\x80", 4);                      /* digitalSignature */
        extension(&exts, ku_oid, 1, &val);
    }
    if (!s->is_ca || s->subject_seed != s->issuer_seed) {
        val.n = inner.n = 0;
        wrap_bytes(&inner, 0x80, issuer_pub, 20);              /* keyIdentifier */
        wrap(&val, 0x30, &inner);
        extension(&exts, aki_oid, 0, &val);
    }
    wrap(&all, 0x30, &exts);
    wrap(&x3, 0xa3, &all);
    put(&tbs, x3.b, x3.n);
    wrap(&t, 0x30, &tbs);

    if (nxc_ed25519_sign(sig, t.b, t.n, s->issuer_seed) != NXC_OK) {
        printf("ERROR: certgen sign\n");
        exit(2);
    }
    dbuf body = { .n = 0 }, cert = { .n = 0 }, sbits = { .n = 0 };
    put(&body, t.b, t.n);
    put(&body, alg_ed25519, sizeof(alg_ed25519));
    put(&sbits, "\x00", 1);
    put(&sbits, sig, 64);
    wrap(&body, 0x03, &sbits);
    wrap(&cert, 0x30, &body);
    if (cert.n > cap) { printf("ERROR: certificate too big\n"); exit(2); }
    memcpy(out, cert.b, cert.n);
    return cert.n;
}

#endif /* CRYPTO_CERTGEN_H */
