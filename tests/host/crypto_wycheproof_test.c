/*
 * Host test: nxcrypto against Project Wycheproof edge-case vectors.
 *
 * The NIST and RFC vectors in the other crypto_*_test programs show that
 * each primitive computes the right answer. Wycheproof adds the inputs
 * that break real libraries: malformed and non-minimal DER signatures,
 * r or s out of range, signatures that are off by a multiple of n,
 * PKCS#1 padding variants, low-order and non-canonical X25519 points,
 * non-canonical Ed25519 encodings, and modified AEAD tags.
 *
 * Each case carries a result: "valid" must verify, "invalid" must be
 * refused, and "acceptable" may go either way (the test only checks that
 * a successful X25519 result is the right one). Groups whose parameters
 * NeolyxOS does not use (AES-192, GCM nonces other than 96 bits) are
 * counted as skipped.
 *
 * Vectors: tests/host/crypto_fetch_vectors.sh downloads a pinned commit
 * of github.com/C2SP/wycheproof (testvectors_v1) with a SHA-256 for every
 * file. Run: make -C lib/nxcrypto test
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include "crypto_vec.h"
#include "crypto_json.h"
#include "nxcrypto.h"

typedef enum { RES_VALID, RES_INVALID, RES_ACCEPTABLE } wp_result;

typedef struct {
    const char *file;
    int count, skipped, valid_ok, invalid_refused;
} wp_stats;

static wp_result result_of(const json *t) {
    const char *r = json_str(t, "result");
    if (r && !strcmp(r, "valid")) return RES_VALID;
    if (r && !strcmp(r, "acceptable")) return RES_ACCEPTABLE;
    if (r && !strcmp(r, "invalid")) return RES_INVALID;
    printf("ERROR: test case without a known result\n");
    exit(2);
}

/* Decode a required hex member; a missing or bad value ends the test. */
static uint8_t *hex_member(const json *obj, const char *key, size_t *len, const char *file) {
    const char *s = json_str(obj, key);
    uint8_t *b = s ? hex_decode(s, len) : NULL;
    if (!b) {
        printf("ERROR: %s: member \"%s\" missing or not hex\n", file, key);
        exit(2);
    }
    return b;
}

static nxc_hash_alg_t wp_alg(const char *s) {
    if (s && !strcmp(s, "SHA-256")) return NXC_HASH_SHA256;
    if (s && !strcmp(s, "SHA-384")) return NXC_HASH_SHA384;
    if (s && !strcmp(s, "SHA-512")) return NXC_HASH_SHA512;
    return NXC_HASH_NONE;
}

/* Record one signature-style outcome against the expected result. */
static void judge(wp_stats *st, const json *t, wp_result res, int accepted, int rc) {
    long id = json_long(t, "tcId", -1);
    st->count++;
    if (res == RES_VALID) {
        CHECK(accepted, "%s tcId %ld (%s): valid case refused, rc %d",
              st->file, id, json_str(t, "comment"), rc);
        st->valid_ok += accepted;
    } else if (res == RES_INVALID) {
        CHECK(!accepted, "%s tcId %ld (%s): invalid case accepted",
              st->file, id, json_str(t, "comment"));
        st->invalid_refused += !accepted;
    }
}

typedef void (*group_fn)(wp_stats *st, const json *group, const json *test);

static void run_wp(const char *dir, const char *file, group_fn fn, int min_count) {
    char rel[256];
    snprintf(rel, sizeof(rel), "wycheproof/%s", file);
    char *text = vec_read_file(dir, rel);
    json *doc = json_parse(text, file);
    json *groups = json_get(doc, "testGroups");
    if (!groups || groups->type != JSON_ARR) {
        printf("ERROR: %s: no testGroups array\n", file);
        exit(2);
    }
    wp_stats st = { file, 0, 0, 0, 0 };
    for (size_t g = 0; g < groups->len; g++) {
        json *tests = json_get(groups->items[g], "tests");
        if (!tests || tests->type != JSON_ARR) continue;
        for (size_t i = 0; i < tests->len; i++) fn(&st, groups->items[g], tests->items[i]);
    }
    long declared = json_long(doc, "numberOfTests", -1);
    CHECK(st.count + st.skipped == declared, "%s: ran %d + skipped %d, file declares %ld",
          file, st.count, st.skipped, declared);
    CHECK(st.count >= min_count, "%s: %d cases, want >= %d", file, st.count, min_count);
    printf("  %-40s %4d cases (%d valid accepted, %d invalid refused)", file, st.count,
           st.valid_ok, st.invalid_refused);
    if (st.skipped) printf(" +%d skipped", st.skipped);
    printf("\n");
    json_free(doc);
    free(text);
}

/* ------------------------------------------------------------------ */
/* Signatures                                                          */
/* ------------------------------------------------------------------ */

static void ecdsa_case(wp_stats *st, const json *g, const json *t) {
    const json *pk = json_get(g, "publicKey");
    const char *curve_name = json_str(pk, "curve");
    nxc_ec_curve_t curve = curve_name && !strcmp(curve_name, "secp256r1") ? NXC_EC_P256 :
                           curve_name && !strcmp(curve_name, "secp384r1") ? NXC_EC_P384 : 0;
    nxc_hash_alg_t alg = wp_alg(json_str(g, "sha"));
    if (curve == 0 || alg == NXC_HASH_NONE) { st->skipped++; return; }

    size_t pub_len, msg_len, sig_len;
    uint8_t *pub = hex_member(pk, "uncompressed", &pub_len, st->file);
    uint8_t *msg = hex_member(t, "msg", &msg_len, st->file);
    uint8_t *sig = hex_member(t, "sig", &sig_len, st->file);
    uint8_t h[64];
    CHECK(nxc_hash(alg, msg, msg_len, h, sizeof(h)) == NXC_OK, "hash");
    int rc = nxc_ecdsa_verify_der(curve, pub, pub_len, h, nxc_hash_digest_len(alg), sig, sig_len);
    judge(st, t, result_of(t), rc == NXC_OK, rc);
    free(pub); free(msg); free(sig);
}

static void rsa_case(wp_stats *st, const json *g, const json *t, int pss) {
    const json *pk = json_get(g, "publicKey");
    nxc_hash_alg_t alg = wp_alg(json_str(g, "sha"));
    if (alg == NXC_HASH_NONE) { st->skipped++; return; }
    long salt = 0;
    if (pss) {
        const char *mgf = json_str(g, "mgf");
        salt = json_long(g, "sLen", -1);
        /* nxc_rsa_verify_pss uses MGF1 with the message hash, as TLS 1.3 does. */
        if (!mgf || strcmp(mgf, "MGF1") || wp_alg(json_str(g, "mgfSha")) != alg || salt < 0) {
            st->skipped++;
            return;
        }
    }
    size_t n_len, e_len, msg_len, sig_len;
    uint8_t *n = hex_member(pk, "modulus", &n_len, st->file);
    uint8_t *e = hex_member(pk, "publicExponent", &e_len, st->file);
    uint8_t *msg = hex_member(t, "msg", &msg_len, st->file);
    uint8_t *sig = hex_member(t, "sig", &sig_len, st->file);
    uint8_t h[64];
    CHECK(nxc_hash(alg, msg, msg_len, h, sizeof(h)) == NXC_OK, "hash");
    size_t hl = nxc_hash_digest_len(alg);
    int rc = pss ? nxc_rsa_verify_pss(n, n_len, e, e_len, alg, h, hl, (size_t)salt, sig, sig_len)
                 : nxc_rsa_verify_pkcs1v15(n, n_len, e, e_len, alg, h, hl, sig, sig_len);
    judge(st, t, result_of(t), rc == NXC_OK, rc);
    free(n); free(e); free(msg); free(sig);
}

static void rsa_pkcs1_case(wp_stats *st, const json *g, const json *t) { rsa_case(st, g, t, 0); }
static void rsa_pss_case(wp_stats *st, const json *g, const json *t) { rsa_case(st, g, t, 1); }

static void ed25519_case(wp_stats *st, const json *g, const json *t) {
    size_t pub_len, msg_len, sig_len;
    uint8_t *pub = hex_member(json_get(g, "publicKey"), "pk", &pub_len, st->file);
    uint8_t *msg = hex_member(t, "msg", &msg_len, st->file);
    uint8_t *sig = hex_member(t, "sig", &sig_len, st->file);
    int rc = NXC_ERR_LENGTH;            /* the API takes fixed sizes; others are refused */
    if (pub_len == NXC_ED25519_PUBLIC_LEN && sig_len == NXC_ED25519_SIGNATURE_LEN)
        rc = nxc_ed25519_verify(sig, msg, msg_len, pub);
    judge(st, t, result_of(t), rc == NXC_OK, rc);
    free(pub); free(msg); free(sig);
}

/* ------------------------------------------------------------------ */
/* X25519                                                              */
/* ------------------------------------------------------------------ */

static void x25519_case(wp_stats *st, const json *g, const json *t) {
    (void)g;
    size_t pub_len, priv_len, shared_len;
    uint8_t *pub = hex_member(t, "public", &pub_len, st->file);
    uint8_t *priv = hex_member(t, "private", &priv_len, st->file);
    uint8_t *shared = hex_member(t, "shared", &shared_len, st->file);
    long id = json_long(t, "tcId", -1);
    wp_result res = result_of(t);
    st->count++;
    if (pub_len != 32 || priv_len != 32 || shared_len != 32) {
        CHECK(res != RES_VALID, "%s tcId %ld: valid case with odd sizes", st->file, id);
    } else {
        uint8_t out[32];
        int rc = nxc_x25519(out, priv, pub);
        int zero = 1;
        for (int i = 0; i < 32; i++) zero &= shared[i] == 0;
        if (zero) {
            /* RFC 7748 section 6.1: an all-zero secret must be rejected. */
            CHECK(rc == NXC_ERR_WEAK, "%s tcId %ld: zero shared secret not refused, rc %d",
                  st->file, id, rc);
            st->invalid_refused += rc == NXC_ERR_WEAK;
        } else {
            CHECK(rc == NXC_OK && !memcmp(out, shared, 32), "%s tcId %ld (%s): wrong shared secret",
                  st->file, id, json_str(t, "comment"));
            st->valid_ok += rc == NXC_OK && !memcmp(out, shared, 32);
        }
        CHECK(res != RES_INVALID || rc != NXC_OK, "%s tcId %ld: invalid case accepted", st->file, id);
    }
    free(pub); free(priv); free(shared);
}

/* ------------------------------------------------------------------ */
/* AEADs                                                               */
/* ------------------------------------------------------------------ */

static void aead_case(wp_stats *st, const json *g, const json *t, int gcm) {
    long key_bits = json_long(g, "keySize", -1), iv_bits = json_long(g, "ivSize", -1);
    long tag_bits = json_long(g, "tagSize", -1);
    /* NeolyxOS (TLS 1.3) uses 96-bit nonces, 128-bit tags and AES-128/256. */
    if (iv_bits != 96 || tag_bits != 128 || (gcm && key_bits != 128 && key_bits != 256) ||
        (!gcm && key_bits != 256)) {
        st->skipped++;
        return;
    }
    size_t key_len, iv_len, aad_len, msg_len, ct_len, tag_len;
    uint8_t *key = hex_member(t, "key", &key_len, st->file);
    uint8_t *iv = hex_member(t, "iv", &iv_len, st->file);
    uint8_t *aad = hex_member(t, "aad", &aad_len, st->file);
    uint8_t *msg = hex_member(t, "msg", &msg_len, st->file);
    uint8_t *ct = hex_member(t, "ct", &ct_len, st->file);
    uint8_t *tag = hex_member(t, "tag", &tag_len, st->file);
    long id = json_long(t, "tcId", -1);
    wp_result res = result_of(t);

    if (iv_len != 12 || tag_len != 16 || ct_len != msg_len) {
        CHECK(res != RES_VALID, "%s tcId %ld: valid case with odd sizes", st->file, id);
        st->count++;
        goto out;
    }
    uint8_t *buf = malloc(msg_len + 1), stag[16];
    if (!buf) { perror("malloc"); exit(2); }
    nxc_gcm_ctx ctx;
    int rc;
    if (gcm) {
        CHECK(nxc_gcm_init(&ctx, key, key_len) == NXC_OK, "%s tcId %ld: gcm_init", st->file, id);
        rc = nxc_gcm_open(&ctx, buf, iv, aad, aad_len, ct, ct_len, tag);
    } else {
        rc = nxc_chacha20poly1305_open(buf, key, iv, aad, aad_len, ct, ct_len, tag);
    }
    int opened = rc == NXC_OK && !memcmp(buf, msg, msg_len);
    if (rc != NXC_OK) {
        /* A refused record must not leave plaintext behind. */
        int clear = 1;
        for (size_t i = 0; i < msg_len; i++) clear &= buf[i] == 0;
        CHECK(clear, "%s tcId %ld: output not wiped after a failed open", st->file, id);
    }
    judge(st, t, res, opened, rc);

    if (res == RES_VALID) {
        rc = gcm ? nxc_gcm_seal(&ctx, buf, stag, iv, aad, aad_len, msg, msg_len)
                 : nxc_chacha20poly1305_seal(buf, stag, key, iv, aad, aad_len, msg, msg_len);
        CHECK(rc == NXC_OK && !memcmp(buf, ct, ct_len) && !memcmp(stag, tag, 16),
              "%s tcId %ld: seal does not reproduce ct/tag", st->file, id);
    }
    if (gcm) nxc_gcm_wipe(&ctx);
    free(buf);
out:
    free(key); free(iv); free(aad); free(msg); free(ct); free(tag);
}

static void gcm_case(wp_stats *st, const json *g, const json *t) { aead_case(st, g, t, 1); }
static void chachapoly_case(wp_stats *st, const json *g, const json *t) { aead_case(st, g, t, 0); }

int main(int argc, char **argv) {
    if (argc != 2) {
        printf("usage: %s <vector dir>\n", argv[0]);
        return 2;
    }
    const char *d = argv[1];
    printf("nxcrypto Wycheproof vectors\n");
    run_wp(d, "ecdsa_secp256r1_sha256_test.json", ecdsa_case, 400);
    run_wp(d, "ecdsa_secp256r1_sha512_test.json", ecdsa_case, 400);
    run_wp(d, "ecdsa_secp384r1_sha384_test.json", ecdsa_case, 400);
    run_wp(d, "ecdsa_secp384r1_sha512_test.json", ecdsa_case, 400);
    run_wp(d, "rsa_signature_2048_sha256_test.json", rsa_pkcs1_case, 200);
    run_wp(d, "rsa_signature_3072_sha384_test.json", rsa_pkcs1_case, 200);
    run_wp(d, "rsa_signature_4096_sha512_test.json", rsa_pkcs1_case, 200);
    run_wp(d, "rsa_pss_2048_sha256_mgf1_32_test.json", rsa_pss_case, 80);
    run_wp(d, "rsa_pss_3072_sha256_mgf1_32_test.json", rsa_pss_case, 80);
    run_wp(d, "rsa_pss_4096_sha512_mgf1_64_test.json", rsa_pss_case, 80);
    run_wp(d, "ed25519_test.json", ed25519_case, 200);
    run_wp(d, "x25519_test.json", x25519_case, 500);
    run_wp(d, "chacha20_poly1305_test.json", chachapoly_case, 200);
    run_wp(d, "aes_gcm_test.json", gcm_case, 100);
    return vec_report("crypto_wycheproof_test");
}
