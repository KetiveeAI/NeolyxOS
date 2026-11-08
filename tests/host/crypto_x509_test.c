/*
 * Host test for nxtls X.509 parsing and certificate signatures.
 *
 * Published certificates (from the pinned vector set, see
 * tests/host/crypto_fetch_vectors.sh):
 *   - the x509 directory of cryptography_vectors: real-world and
 *     hand-made edge-case certificates (bad versions, bad times, malformed
 *     extensions, unusual algorithms);
 *   - NIST PKITS (Public Key Interoperability Test Suite) certificates.
 * Every file is parsed (run under ASan, this catches memory errors on
 * hostile input); selected files have pinned results, field values and
 * host name checks; every PKITS certificate's signature is checked
 * against its issuer. Path validation is tested by
 * crypto_x509_limbo_test.
 *
 * Run: make -C lib/nxtls test           (list mode: argv[2] = "list")
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include "crypto_vec.h"
#include "crypto_pem.h"
#include "nxtls/x509.h"
#include "nxtls/x509_verify.h"
#include "nxcrypto.h"

#include <dirent.h>

static int g_list;

/* Load a certificate file: PEM (first CERTIFICATE block) or raw DER. */
static uint8_t *load_cert(const char *dir, const char *rel, size_t *len) {
    char *path = vec_path(dir, rel);
    FILE *f = fopen(path, "rb");
    if (!f) {
        printf("ERROR: cannot open %s\n", path);
        exit(2);
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)sz + 1);
    if (!buf || fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        printf("ERROR: cannot read %s\n", path);
        exit(2);
    }
    buf[sz] = 0;
    fclose(f);
    free(path);
    if (strstr(buf, "-----BEGIN CERTIFICATE-----")) {
        const char *pos = buf;
        uint8_t *der = pem_next(&pos, "CERTIFICATE", len);
        free(buf);
        return der;
    }
    *len = (size_t)sz;
    return (uint8_t *)buf;
}

/* 1 if the file is PEM but holds no certificate (a CRL, CSR or key). */
static int is_other_pem(const char *dir, const char *rel) {
    char *text = vec_read_file(dir, rel);
    int other = strstr(text, "-----BEGIN ") != NULL && strstr(text, "-----BEGIN CERTIFICATE-----") == NULL;
    free(text);
    return other;
}

static int parse_file(const char *dir, const char *rel, nxt_x509_cert *c, uint8_t **der) {
    size_t len;
    *der = load_cert(dir, rel, &len);
    if (!*der) return -999;
    return nxt_x509_parse(c, *der, len);
}

/* ------------------------------------------------------------------ */
/* Pinned results for single certificates                              */
/* ------------------------------------------------------------------ */

static void expect_parse(const char *dir, const char *rel, int want) {
    nxt_x509_cert c;
    uint8_t *der;
    int rc = parse_file(dir, rel, &c, &der);
    CHECK(rc == want, "%s: parse returned %d (%s), want %d (%s)", rel, rc,
          nxt_x509_err_str(rc), want, nxt_x509_err_str(want));
    free(der);
}

/* Field values below were read from the published files (any DER dump
 * tool shows them); times are seconds since 1970 UTC. */
static void test_fields(const char *d) {
    nxt_x509_cert c;
    uint8_t *der;

    /* A 2014 RSA-4096 server certificate for cryptography.io. */
    CHECK(parse_file(d, "x509/cryptography.io.pem", &c, &der) == NXT_X509_OK, "cryptography.io parses");
    CHECK(c.key_type == NXT_KEY_RSA && c.key_bits == 4096, "cryptography.io: RSA-4096");
    CHECK(c.sig_alg == NXT_SIG_RSA_PKCS1_SHA256, "cryptography.io: sha256WithRSA");
    CHECK(c.serial.len == 2 && c.serial.p[0] == 0x3f && c.serial.p[1] == 0x20, "cryptography.io: serial 3F20");
    CHECK(c.not_before == 1413374972 && c.not_after == 1542330903, "cryptography.io: dates");
    CHECK(!c.is_ca && c.path_len == -1, "cryptography.io: not a CA");
    CHECK(c.ext_key_usage == (NXT_EKU_SERVER_AUTH | NXT_EKU_CLIENT_AUTH), "cryptography.io: EKU");
    CHECK(c.key_usage == (NXT_KU_DIGITAL_SIGNATURE | NXT_KU_KEY_ENCIPHERMENT), "cryptography.io: KU");
    CHECK(nxt_x509_check_host(&c, "cryptography.io") == NXT_X509_OK, "host cryptography.io");
    CHECK(nxt_x509_check_host(&c, "WWW.Cryptography.IO") == NXT_X509_OK, "host is case-insensitive");
    CHECK(nxt_x509_check_host(&c, "www.cryptography.io.") == NXT_X509_OK, "host with trailing dot");
    CHECK(nxt_x509_check_host(&c, "docs.cryptography.io") == NXT_X509_ERR_HOST_MISMATCH, "host docs.");
    CHECK(nxt_x509_check_host(&c, "cryptography.io.evil.com") == NXT_X509_ERR_HOST_MISMATCH, "suffix");
    CHECK(nxt_x509_check_host(&c, "xcryptography.io") == NXT_X509_ERR_HOST_MISMATCH, "prefix");
    CHECK(nxt_x509_check_host(&c, "") == NXT_X509_ERR_BAD_HOST, "empty host");
    CHECK(nxt_x509_check_host(&c, "a..b") == NXT_X509_ERR_BAD_HOST, "empty label");
    CHECK(nxt_x509_check_host(&c, "1.2.3.256") == NXT_X509_ERR_BAD_HOST, "numeric last label");
    free(der);

    /* Wildcards: *.langui.sh matches one label only. */
    CHECK(parse_file(d, "x509/wildcard_san.pem", &c, &der) == NXT_X509_OK, "wildcard_san parses");
    CHECK(nxt_x509_check_host(&c, "www.langui.sh") == NXT_X509_OK, "*.langui.sh covers www");
    CHECK(nxt_x509_check_host(&c, "langui.sh") == NXT_X509_OK, "langui.sh listed itself");
    CHECK(nxt_x509_check_host(&c, "a.b.langui.sh") == NXT_X509_ERR_HOST_MISMATCH, "wildcard: one label");
    CHECK(nxt_x509_check_host(&c, ".langui.sh") == NXT_X509_ERR_BAD_HOST, "empty first label");
    CHECK(nxt_x509_check_host(&c, "x.saseliminator.com") == NXT_X509_OK, "second wildcard");
    free(der);

    /* Let's Encrypt X3 intermediate (2016). */
    CHECK(parse_file(d, "x509/letsencryptx3.pem", &c, &der) == NXT_X509_OK, "letsencryptx3 parses");
    CHECK(c.is_ca && c.path_len == 0, "X3: CA, pathlen 0");
    CHECK(c.key_usage == (NXT_KU_DIGITAL_SIGNATURE | NXT_KU_KEY_CERT_SIGN | NXT_KU_CRL_SIGN), "X3: KU");
    CHECK(c.key_type == NXT_KEY_RSA && c.key_bits == 2048, "X3: RSA-2048");
    CHECK(c.serial.len == 16, "X3: 16-byte serial");
    free(der);

    /* DigiCert Global Root G3: self-signed ECDSA P-384 / SHA-384. */
    CHECK(parse_file(d, "x509/ecdsa_root.pem", &c, &der) == NXT_X509_OK, "ecdsa_root parses");
    CHECK(c.key_type == NXT_KEY_EC_P384 && c.sig_alg == NXT_SIG_ECDSA_SHA384, "G3: P-384, ECDSA-SHA384");
    CHECK(c.not_before == 1375358400 && c.not_after == 2147169600, "G3: dates");
    CHECK(nxt_x509_check_signature(&c, &c) == NXT_X509_OK, "G3: self-signature verifies");
    CHECK(nxt_x509_name_equal(c.issuer, c.subject), "G3: self-issued");
    {   /* flip one signature byte: must not verify */
        size_t len;
        uint8_t *bad = load_cert(d, "x509/ecdsa_root.pem", &len);
        nxt_x509_cert b;
        bad[len - 5] ^= 0x01;
        CHECK(nxt_x509_parse(&b, bad, len) == NXT_X509_OK &&
              nxt_x509_check_signature(&b, &b) == NXT_X509_ERR_BAD_SIGNATURE, "G3: tampered signature");
        bad[len - 5] ^= 0x01;
        bad[200] ^= 0x01;           /* inside the TBS (public key bytes) */
        int rc = nxt_x509_parse(&b, bad, len);
        CHECK(rc != NXT_X509_OK || nxt_x509_check_signature(&b, &c) != NXT_X509_OK, "G3: tampered TBS");
        free(bad);
    }
    free(der);

    /* Ed25519 root, self-signed. */
    CHECK(parse_file(d, "x509/ed25519/root-ed25519.pem", &c, &der) == NXT_X509_OK, "Ed25519 root parses");
    CHECK(c.key_type == NXT_KEY_ED25519 && c.sig_alg == NXT_SIG_ED25519, "Ed25519 root: key and alg");
    CHECK(c.not_after == 2244317799, "Ed25519 root: notAfter in 2041 (GeneralizedTime)");
    CHECK(nxt_x509_check_signature(&c, &c) == NXT_X509_OK, "Ed25519 root: self-signature");
    free(der);

    /* Refusals, each for its own reason. */
    expect_parse(d, "x509/cryptography.io.precert.pem", NXT_X509_ERR_CRITICAL_EXT);   /* CT poison */
    expect_parse(d, "x509/unique_identifier.pem", NXT_X509_ERR_CRITICAL_EXT);         /* critical policies */
    expect_parse(d, "x509/verisign_md2_root.pem", NXT_X509_ERR_SIG_ALG);              /* MD2 */
    expect_parse(d, "x509/v1_cert.pem", NXT_X509_ERR_SIG_ALG);                        /* SHA-1, v1 */
    expect_parse(d, "x509/custom/negative_serial.pem", NXT_X509_ERR_ENCODING);
    expect_parse(d, "x509/custom/empty-eku.pem", NXT_X509_ERR_EXTENSION);
    expect_parse(d, "x509/custom/san_registered_id.pem", NXT_X509_ERR_WEAK_KEY);       /* RSA-1024 */
    expect_parse(d, "x509/custom/rsa_pss_cert.pem", NXT_X509_ERR_KEY);                 /* id-RSASSA-PSS key */
    expect_parse(d, "x509/custom/ec_no_named_curve.pem", NXT_X509_ERR_VERSION);
    expect_parse(d, "x509/custom/nc_permitted.pem", NXT_X509_ERR_EXTENSION);           /* ".cryptography.io" */
    expect_parse(d, "x509/custom/nc_ip_invalid_length.pem", NXT_X509_ERR_EXTENSION);
    expect_parse(d, "x509/custom/nc_invalid_ip_netmask.pem", NXT_X509_ERR_EXTENSION);
    expect_parse(d, "x509/custom/san_empty_hostname.pem", NXT_X509_ERR_EXTENSION);
    expect_parse(d, "x509/PKITS_data/certs/InvalidNegativeSerialNumberTest15EE.crt", NXT_X509_ERR_ENCODING);
    expect_parse(d, "x509/PKITS_data/certs/BadSignedCACert.crt", NXT_X509_ERR_ENCODING);  /* 1 unused bit */
    expect_parse(d, "x509/PKITS_data/certs/InvalidUnknownCriticalCertificateExtensionTest2EE.crt",
                 NXT_X509_ERR_CRITICAL_EXT);
    expect_parse(d, "x509/PKITS_data/certs/ValidUnknownNotCriticalCertificateExtensionTest1EE.crt",
                 NXT_X509_OK);
    expect_parse(d, "x509/PKITS_data/certs/ValidGeneralizedTimenotAfterDateTest8EE.crt", NXT_X509_OK);
    expect_parse(d, "x509/PKITS_data/certs/DSACACert.crt", NXT_X509_ERR_KEY);
}

/* Minimal TLV step for test-side DER walking: returns the content and
 * moves *p past the element, or NULL on a malformed header. */
static const uint8_t *tlv(const uint8_t **p, const uint8_t *end, size_t *clen, size_t *whole) {
    const uint8_t *s = *p;
    if (end - s < 2) return NULL;
    size_t len = s[1], hdr = 2;
    if (len & 0x80) {
        size_t k = len & 0x7f;
        if (k == 0 || k > 3 || (size_t)(end - s) < 2 + k) return NULL;
        len = 0;
        for (size_t i = 0; i < k; i++) len = len << 8 | s[2 + i];
        hdr += k;
    }
    if ((size_t)(end - s) < hdr + len) return NULL;
    *clen = len;
    if (whole) *whole = hdr + len;
    *p = s + hdr + len;
    return s + hdr;
}

/* Subject Name (full DER) of a certificate, without validating anything. */
static int raw_subject(const uint8_t *der, size_t len, nxt_slice *out) {
    const uint8_t *p = der, *end = der + len;
    size_t cl, wl;
    const uint8_t *cert = tlv(&p, end, &cl, NULL);
    if (!cert) return -1;
    p = cert;
    const uint8_t *tbs = tlv(&p, cert + cl, &cl, NULL);
    if (!tbs) return -1;
    p = tbs;
    end = tbs + cl;
    if (p < end && *p == 0xa0 && !tlv(&p, end, &cl, NULL)) return -1;   /* version */
    for (int i = 0; i < 4; i++)                     /* serial, sigalg, issuer, validity */
        if (!tlv(&p, end, &cl, NULL)) return -1;
    const uint8_t *start = p;
    if (!tlv(&p, end, &cl, &wl)) return -1;
    out->p = start;
    out->len = wl;
    return 0;
}

/*
 * Every PKITS certificate that parses is checked against each PKITS
 * certificate whose subject is its issuer: at least one must verify, except
 * for the one test whose end-entity signature is deliberately broken.
 */
static void test_pkits_signatures(const char *d) {
    const char *sub = "x509/PKITS_data/certs";
    char *path = vec_path(d, sub);
    DIR *dir = opendir(path);
    if (!dir) {
        printf("ERROR: cannot open %s\n", path);
        exit(2);
    }
    enum { MAXC = 512 };
    static nxt_x509_cert certs[MAXC];
    static uint8_t *ders[MAXC];
    static char names[MAXC][256];
    static uint8_t *bad_ders[MAXC];
    static nxt_slice bad_subjects[MAXC];
    size_t n = 0, n_bad = 0;
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL && n < MAXC) {
        size_t nl = strlen(ent->d_name);
        if (nl < 5 || strcmp(ent->d_name + nl - 4, ".crt")) continue;
        char rel[512];
        snprintf(rel, sizeof(rel), "%s/%s", sub, ent->d_name);
        size_t len;
        uint8_t *der = load_cert(d, rel, &len);
        if (der && nxt_x509_parse(&certs[n], der, len) == NXT_X509_OK) {
            ders[n] = der;
            snprintf(names[n], sizeof(names[n]), "%s", ent->d_name);
            n++;
        } else if (der && n_bad < MAXC && raw_subject(der, len, &bad_subjects[n_bad]) == 0) {
            bad_ders[n_bad++] = der;          /* refused CA: remember its name */
        } else {
            free(der);
        }
    }
    closedir(dir);
    free(path);

    int verified = 0, refused = 0, orphans = 0;
    for (size_t i = 0; i < n; i++) {
        int any = 0, ok = 0;
        for (size_t j = 0; j < n; j++) {
            if (!nxt_x509_name_equal(certs[j].subject, certs[i].issuer)) continue;
            any = 1;
            if (nxt_x509_check_signature(&certs[i], &certs[j]) == NXT_X509_OK) ok = 1;
        }
        if (!any) { orphans++; continue; }      /* name-chaining tests: no such issuer */
        int issuer_refused = 0;                 /* a same-named CA did not load */
        for (size_t j = 0; j < n_bad; j++)
            if (nxt_x509_name_equal(bad_subjects[j], certs[i].issuer)) issuer_refused = 1;
        if (!ok && issuer_refused) { orphans++; continue; }
        int must_fail = strstr(names[i], "InvalidEESignatureTest3") != NULL;
        CHECK(ok != must_fail, "PKITS %s: signature %s", names[i], ok ? "verified" : "did not verify");
        verified += ok;
        refused += !ok;
    }
    CHECK(verified > 280, "only %d PKITS signatures verified", verified);
    printf("  PKITS signatures: %d verified, %d refused (InvalidEESignatureTest3), %d without a loadable issuer\n",
           verified, refused, orphans);
    for (size_t i = 0; i < n; i++) free(ders[i]);
    for (size_t i = 0; i < n_bad; i++) free(bad_ders[i]);
}

/* ------------------------------------------------------------------ */
/* A self-signed server certificate, built here with nxcrypto Ed25519  */
/* ------------------------------------------------------------------ */

typedef struct { uint8_t b[2048]; size_t n; } dbuf;

static void put(dbuf *d, const void *p, size_t n) {
    if (d->n + n > sizeof(d->b)) { printf("ERROR: dbuf overflow\n"); exit(2); }
    memcpy(d->b + d->n, p, n);
    d->n += n;
}

/* Append tag, DER length and the contents of `in`. */
static void wrap(dbuf *out, uint8_t tag, const dbuf *in) {
    uint8_t h[4] = { tag };
    size_t hl;
    if (in->n < 0x80) { h[1] = (uint8_t)in->n; hl = 2; }
    else if (in->n < 0x100) { h[1] = 0x81; h[2] = (uint8_t)in->n; hl = 3; }
    else { h[1] = 0x82; h[2] = (uint8_t)(in->n >> 8); h[3] = (uint8_t)in->n; hl = 4; }
    put(out, h, hl);
    put(out, in->b, in->n);
}

static void wrap_bytes(dbuf *out, uint8_t tag, const void *p, size_t n) {
    dbuf t = { .n = 0 };
    put(&t, p, n);
    wrap(out, tag, &t);
}

static void extension(dbuf *exts, const uint8_t oid[3], int critical, const dbuf *value) {
    dbuf e = { .n = 0 };
    wrap_bytes(&e, 0x06, oid, 3);
    if (critical) put(&e, "\x01\x01\xff", 3);
    wrap(&e, 0x04, value);
    wrap(exts, 0x30, &e);
}

/* Self-signed Ed25519 certificate for "self.example", valid 2025-2035,
 * shaped like a real server certificate (SAN, EKU serverAuth, KU, AKI). */
static size_t make_self_signed(uint8_t *out, size_t cap) {
    static const uint8_t seed[32] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
                                      17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32 };
    static const uint8_t alg_ed25519[] = { 0x30, 0x05, 0x06, 0x03, 0x2b, 0x65, 0x70 };
    static const uint8_t cn_oid[] = { 0x55, 0x04, 0x03 };
    static const uint8_t san_oid[3] = { 0x55, 0x1d, 0x11 }, eku_oid[3] = { 0x55, 0x1d, 0x25 },
                         ku_oid[3] = { 0x55, 0x1d, 0x0f }, aki_oid[3] = { 0x55, 0x1d, 0x23 };
    static const uint8_t server_auth[] = { 0x2b, 0x06, 0x01, 0x05, 0x05, 0x07, 0x03, 0x01 };
    const char *host = "self.example";
    uint8_t pub[32], sig[64];
    CHECK(nxc_ed25519_public(pub, seed) == NXC_OK, "self-signed: key");

    dbuf atv = { .n = 0 }, rdn = { .n = 0 }, rdns = { .n = 0 }, name = { .n = 0 };
    wrap_bytes(&atv, 0x06, cn_oid, 3);
    wrap_bytes(&atv, 0x0c, host, strlen(host));
    wrap(&rdn, 0x30, &atv);
    wrap(&rdns, 0x31, &rdn);
    wrap(&name, 0x30, &rdns);

    dbuf tbs = { .n = 0 }, t = { .n = 0 }, v = { .n = 0 }, spki = { .n = 0 }, bits = { .n = 0 };
    put(&tbs, "\xa0\x03\x02\x01\x02", 5);                   /* version v3 */
    put(&tbs, "\x02\x01\x2a", 3);                             /* serial 42 */
    put(&tbs, alg_ed25519, sizeof(alg_ed25519));
    put(&tbs, name.b, name.n);                                   /* issuer = subject */
    wrap_bytes(&v, 0x17, "250101000000Z", 13);
    wrap_bytes(&v, 0x17, "350101000000Z", 13);
    wrap(&tbs, 0x30, &v);
    put(&tbs, name.b, name.n);
    put(&spki, alg_ed25519, sizeof(alg_ed25519));
    put(&bits, "\x00", 1);
    put(&bits, pub, 32);
    wrap(&spki, 0x03, &bits);
    wrap(&tbs, 0x30, &spki);

    dbuf exts = { .n = 0 }, val = { .n = 0 }, inner = { .n = 0 }, all = { .n = 0 }, x3 = { .n = 0 };
    wrap_bytes(&inner, 0x82, host, strlen(host));
    wrap(&val, 0x30, &inner);
    extension(&exts, san_oid, 0, &val);
    val.n = inner.n = 0;
    wrap_bytes(&inner, 0x06, server_auth, sizeof(server_auth));
    wrap(&val, 0x30, &inner);
    extension(&exts, eku_oid, 0, &val);
    val.n = 0;
    put(&val, "\x03\x02\x07\x80", 4);                        /* digitalSignature */
    extension(&exts, ku_oid, 1, &val);
    val.n = inner.n = 0;
    wrap_bytes(&inner, 0x80, pub, 20);                           /* keyIdentifier */
    wrap(&val, 0x30, &inner);
    extension(&exts, aki_oid, 0, &val);
    wrap(&all, 0x30, &exts);
    wrap(&x3, 0xa3, &all);
    put(&tbs, x3.b, x3.n);
    wrap(&t, 0x30, &tbs);

    CHECK(nxc_ed25519_sign(sig, t.b, t.n, seed) == NXC_OK, "self-signed: sign");
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

static void test_self_signed(const char *d) {
    static uint8_t der[2048];
    size_t len = make_self_signed(der, sizeof(der));
    nxt_x509_cert self, other;
    CHECK(nxt_x509_parse(&self, der, len) == NXT_X509_OK, "self-signed: parses");
    CHECK(nxt_x509_check_signature(&self, &self) == NXT_X509_OK, "self-signed: own signature verifies");
    CHECK(nxt_x509_check_host(&self, "self.example") == NXT_X509_OK, "self-signed: names its host");

    uint8_t *root_der;
    CHECK(parse_file(d, "x509/ecdsa_root.pem", &other, &root_der) == NXT_X509_OK, "root loads");
    nxt_trust_store ts = { &other, 1 };
    nxt_verify_opts o = { 1767225600 /* 2026-01-01 */, "self.example", 0 };
    nxt_verify_result r;
    CHECK(nxt_x509_verify_server(&self, 1, &ts, &o, &r) == NXT_X509_ERR_UNKNOWN_ISSUER &&
          r.failed_index == 0, "self-signed server certificate refused: %s", nxt_x509_err_str(r.err));
    /* Even if someone puts it in the trust store, a non-CA is no anchor. */
    nxt_trust_store own = { &self, 1 };
    CHECK(nxt_x509_verify_server(&self, 1, &own, &o, &r) == NXT_X509_ERR_NOT_CA,
          "self-signed certificate in the trust store still refused: %s", nxt_x509_err_str(r.err));
    free(root_der);
}

/* Walk every certificate file in a directory: parse must not crash and
 * must return a defined code. In list mode, print each result. */
static int walk_dir(const char *dir, const char *sub, int *ok_count) {
    char *path = vec_path(dir, sub);
    DIR *d = opendir(path);
    if (!d) {
        printf("ERROR: cannot open directory %s\n", path);
        exit(2);
    }
    int n = 0;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        const char *name = ent->d_name;
        size_t nl = strlen(name);
        if (nl < 5) continue;
        const char *ext = name + nl - 4;
        if (strcmp(ext, ".pem") && strcmp(ext, ".der") && strcmp(ext, ".crt")) continue;
        char rel[512];
        snprintf(rel, sizeof(rel), "%s/%s", sub, name);
        size_t len;
        if (is_other_pem(dir, rel)) continue;   /* CRL, CSR or key file */
        uint8_t *der = load_cert(dir, rel, &len);
        if (!der) continue;
        nxt_x509_cert c;
        int rc = nxt_x509_parse(&c, der, len);
        CHECK(rc <= 0 && rc >= NXT_X509_ERR_BAD_HOST, "%s: undefined code %d", rel, rc);
        if (rc == 0) (*ok_count)++;
        if (g_list) printf("    %-60s %d %s\n", rel, rc, nxt_x509_err_str(rc));
        free(der);
        n++;
    }
    closedir(d);
    free(path);
    return n;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("usage: %s <vector dir> [list]\n", argv[0]);
        return 2;
    }
    const char *d = argv[1];
    g_list = argc > 2 && !strcmp(argv[2], "list");
    printf("nxtls X.509 parsing\n");

    int ok = 0, n;
    n = walk_dir(d, "x509", &ok);
    printf("  x509/: %d certificates, %d accepted\n", n, ok);
    ok = 0;
    n = walk_dir(d, "x509/custom", &ok);
    printf("  x509/custom/: %d certificates, %d accepted\n", n, ok);
    ok = 0;
    n = walk_dir(d, "x509/PKITS_data/certs", &ok);
    printf("  x509/PKITS_data/certs/: %d certificates, %d accepted\n", n, ok);
    test_fields(d);
    test_self_signed(d);
    test_pkits_signatures(d);
    return vec_report("crypto_x509_test");
}
