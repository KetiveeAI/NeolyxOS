/*
 * Host test for nxcrypto SHA-256/384/512, HMAC and HKDF.
 *
 * Published vectors:
 *   NIST CAVP SHAVS: SHA{256,384,512}{ShortMsg,LongMsg,Monte}.rsp
 *   RFC 4231 HMAC-SHA-256/384/512 test cases
 *   RFC 5869 HKDF-SHA-256 test cases
 * plus checks that need no vectors: every split point of streaming input
 * gives the one-shot digest, and bad arguments return the right status.
 *
 * Build and run (from the repo root):
 *   make -C lib/nxcrypto test
 * or by hand:
 *   V=$(tests/host/crypto_fetch_vectors.sh)
 *   make -C lib/nxcrypto build/host/crypto_sha2_test
 *   lib/nxcrypto/build/host/crypto_sha2_test "$V"
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include "crypto_vec.h"
#include "nxcrypto.h"

/* ------------------------------------------------------------------ */
/* SHAVS ShortMsg / LongMsg                                            */
/* ------------------------------------------------------------------ */

typedef struct {
    nxc_hash_alg_t alg;
    const char *file;
    long len_bits;
    char *msg;
    int count;
} msg_state;

static void msg_cb(void *vctx, const char *name, const char *value) {
    msg_state *s = vctx;
    if (!strcmp(name, "Len")) {
        s->len_bits = atol(value);
    } else if (!strcmp(name, "Msg")) {
        free(s->msg);
        s->msg = vec_strdup(value);
    } else if (!strcmp(name, "MD") && s->msg) {
        size_t mlen = 0, dlen = 0;
        uint8_t *m = hex_decode(s->msg, &mlen);
        uint8_t *want = hex_decode(value, &dlen);
        size_t n = (size_t)(s->len_bits / 8);   /* Len = 0 has Msg = 00 */
        uint8_t got[64];
        CHECK(m && want && n <= mlen, "%s: bad record", s->file);
        if (m && want && n <= mlen) {
            int rc = nxc_hash(s->alg, m, n, got, sizeof(got));
            CHECK(rc == NXC_OK && dlen == nxc_hash_digest_len(s->alg) &&
                  memcmp(got, want, dlen) == 0,
                  "%s Len=%ld", s->file, s->len_bits);
            s->count++;
        }
        free(m);
        free(want);
        free(s->msg);
        s->msg = NULL;
    }
}

static void run_msg_file(const char *dir, nxc_hash_alg_t alg, const char *file) {
    msg_state s = { alg, file, 0, NULL, 0 };
    char *text = vec_read_file(dir, file);
    vec_parse(text, msg_cb, &s);
    free(text);
    free(s.msg);
    CHECK(s.count > 0, "%s: no vectors found", file);
    printf("  %-34s %d vectors\n", file, s.count);
}

/* ------------------------------------------------------------------ */
/* SHAVS Monte Carlo (SHAVS section 6.4):                              */
/*   MD0 = MD1 = MD2 = Seed                                            */
/*   for i in 3..1002: MDi = H(MD[i-3] || MD[i-2] || MD[i-1])          */
/*   checkpoint = MD1002, next Seed = checkpoint (100 checkpoints)     */
/* ------------------------------------------------------------------ */

typedef struct {
    nxc_hash_alg_t alg;
    const char *file;
    uint8_t seed[64];
    int have_seed;
    int count;
} mc_state;

static void mc_cb(void *vctx, const char *name, const char *value) {
    mc_state *s = vctx;
    size_t dl = nxc_hash_digest_len(s->alg);
    if (!strcmp(name, "Seed")) {
        s->have_seed = hex_fixed(value, s->seed, dl) == 0;
    } else if (!strcmp(name, "MD") && s->have_seed) {
        uint8_t md[3][64], m[192], want[64];
        memcpy(md[0], s->seed, dl);
        memcpy(md[1], s->seed, dl);
        memcpy(md[2], s->seed, dl);
        for (int i = 3; i < 1003; i++) {
            memcpy(m, md[0], dl);
            memcpy(m + dl, md[1], dl);
            memcpy(m + 2 * dl, md[2], dl);
            memcpy(md[0], md[1], dl);
            memcpy(md[1], md[2], dl);
            nxc_hash(s->alg, m, 3 * dl, md[2], 64);
        }
        CHECK(hex_fixed(value, want, dl) == 0 && memcmp(md[2], want, dl) == 0,
              "%s checkpoint %d", s->file, s->count);
        memcpy(s->seed, md[2], dl);
        s->count++;
    }
}

static void run_monte(const char *dir, nxc_hash_alg_t alg, const char *file) {
    mc_state s;
    memset(&s, 0, sizeof(s));
    s.alg = alg;
    s.file = file;
    char *text = vec_read_file(dir, file);
    vec_parse(text, mc_cb, &s);
    free(text);
    CHECK(s.count == 100, "%s: %d checkpoints, want 100", file, s.count);
    printf("  %-34s %d checkpoints\n", file, s.count);
}

/* ------------------------------------------------------------------ */
/* HMAC (RFC 4231)                                                     */
/* ------------------------------------------------------------------ */

typedef struct {
    nxc_hash_alg_t alg;
    const char *file;
    char *key, *msg;
    int count;
} hmac_state;

static void hmac_cb(void *vctx, const char *name, const char *value) {
    hmac_state *s = vctx;
    if (!strcmp(name, "Key")) { free(s->key); s->key = vec_strdup(value); }
    else if (!strcmp(name, "Msg")) { free(s->msg); s->msg = vec_strdup(value); }
    else if (!strcmp(name, "MD") && s->key && s->msg) {
        size_t kl, ml, wl;
        uint8_t *k = hex_decode(s->key, &kl), *m = hex_decode(s->msg, &ml);
        uint8_t *w = hex_decode(value, &wl);
        uint8_t got[64];
        int rc = nxc_hmac(s->alg, k, kl, m, ml, got, sizeof(got));
        CHECK(rc == NXC_OK && wl == nxc_hash_digest_len(s->alg) &&
              memcmp(got, w, wl) == 0, "%s case %d", s->file, s->count + 1);

        /* Same result when fed one byte at a time. */
        nxc_hmac_ctx c;
        rc = nxc_hmac_init(&c, s->alg, k, kl);
        for (size_t i = 0; rc == NXC_OK && i < ml; i++) rc = nxc_hmac_update(&c, m + i, 1);
        if (rc == NXC_OK) rc = nxc_hmac_final(&c, got, sizeof(got));
        CHECK(rc == NXC_OK && memcmp(got, w, wl) == 0, "%s case %d bytewise",
              s->file, s->count + 1);
        free(k); free(m); free(w);
        free(s->key); free(s->msg);
        s->key = s->msg = NULL;
        s->count++;
    }
}

static void run_hmac(const char *dir, nxc_hash_alg_t alg, const char *file) {
    hmac_state s = { alg, file, NULL, NULL, 0 };
    char *text = vec_read_file(dir, file);
    vec_parse(text, hmac_cb, &s);
    free(text);
    free(s.key); free(s.msg);
    CHECK(s.count >= 6, "%s: only %d vectors", file, s.count);
    printf("  %-34s %d vectors\n", file, s.count);
}

/* ------------------------------------------------------------------ */
/* HKDF (RFC 5869 appendix A)                                          */
/* ------------------------------------------------------------------ */

typedef struct {
    char *ikm, *salt, *info, *prk;
    long L;
    int count;
} hkdf_state;

static void hkdf_cb(void *vctx, const char *name, const char *value) {
    hkdf_state *s = vctx;
    char **slot = NULL;
    if (!strcmp(name, "IKM")) slot = &s->ikm;
    else if (!strcmp(name, "salt")) slot = &s->salt;
    else if (!strcmp(name, "info")) slot = &s->info;
    else if (!strcmp(name, "PRK")) slot = &s->prk;
    else if (!strcmp(name, "L")) s->L = atol(value);
    if (slot) { free(*slot); *slot = vec_strdup(value); return; }

    if (!strcmp(name, "OKM") && s->ikm && s->prk) {
        size_t il, sl = 0, nl = 0, pl, ol;
        uint8_t *ikm = hex_decode(s->ikm, &il);
        uint8_t *salt = s->salt ? hex_decode(s->salt, &sl) : NULL;
        uint8_t *info = s->info ? hex_decode(s->info, &nl) : NULL;
        uint8_t *prk_w = hex_decode(s->prk, &pl);
        uint8_t *okm_w = hex_decode(value, &ol);
        uint8_t prk[32], okm[512];
        int rc = nxc_hkdf_extract(NXC_HASH_SHA256, salt, sl, ikm, il, prk, sizeof(prk));
        CHECK(rc == NXC_OK && pl == 32 && memcmp(prk, prk_w, 32) == 0,
              "HKDF case %d PRK", s->count + 1);
        rc = nxc_hkdf_expand(NXC_HASH_SHA256, prk, 32, info, nl, okm, (size_t)s->L);
        CHECK(rc == NXC_OK && ol == (size_t)s->L && memcmp(okm, okm_w, ol) == 0,
              "HKDF case %d OKM", s->count + 1);
        free(ikm); free(salt); free(info); free(prk_w); free(okm_w);
        free(s->ikm); free(s->salt); free(s->info); free(s->prk);
        s->ikm = s->salt = s->info = s->prk = NULL;
        s->count++;
    }
}

static void run_hkdf(const char *dir) {
    const char *file = "KDF/rfc-5869-HKDF-SHA256.txt";
    hkdf_state s;
    memset(&s, 0, sizeof(s));
    char *text = vec_read_file(dir, file);
    vec_parse(text, hkdf_cb, &s);
    free(text);
    CHECK(s.count == 3, "%s: %d vectors, want 3", file, s.count);
    printf("  %-34s %d vectors\n", file, s.count);
}

/* ------------------------------------------------------------------ */
/* Checks without vectors                                              */
/* ------------------------------------------------------------------ */

static void test_streaming(void) {
    static const nxc_hash_alg_t algs[3] = { NXC_HASH_SHA256, NXC_HASH_SHA384, NXC_HASH_SHA512 };
    uint8_t msg[300];
    for (size_t i = 0; i < sizeof(msg); i++) msg[i] = (uint8_t)(i * 7 + 3);

    for (int a = 0; a < 3; a++) {
        uint8_t ref[64], got[64];
        for (size_t len = 0; len <= sizeof(msg); len += 13) {
            nxc_hash(algs[a], msg, len, ref, sizeof(ref));
            for (size_t cut = 0; cut <= len; cut++) {
                nxc_hash_ctx c;
                nxc_hash_init(&c, algs[a]);
                nxc_hash_update(&c, msg, cut);
                nxc_hash_ctx fork = c;          /* contexts copy by value */
                nxc_hash_update(&c, msg + cut, len - cut);
                nxc_hash_final(&c, got, sizeof(got));
                CHECK(memcmp(got, ref, nxc_hash_digest_len(algs[a])) == 0,
                      "alg %d len %zu split %zu", a, len, cut);
                nxc_hash_update(&fork, msg + cut, len - cut);
                nxc_hash_final(&fork, got, sizeof(got));
                CHECK(memcmp(got, ref, nxc_hash_digest_len(algs[a])) == 0,
                      "alg %d len %zu split %zu (copy)", a, len, cut);
            }
        }
    }
}

static void test_errors(void) {
    uint8_t out[64];
    nxc_sha256_ctx c256;
    nxc_hash_ctx hc;

    CHECK(nxc_sha256_init(NULL) == NXC_ERR_ARG, "sha256_init(NULL)");
    CHECK(nxc_sha256(NULL, 1, out) == NXC_ERR_ARG, "sha256(NULL, 1)");
    CHECK(nxc_sha256(NULL, 0, out) == NXC_OK, "sha256(NULL, 0) is the empty message");

    nxc_sha256_init(&c256);
    nxc_sha256_final(&c256, out);
    CHECK(nxc_sha256_update(&c256, "x", 1) == NXC_ERR_STATE, "update after final");
    CHECK(nxc_sha256_final(&c256, out) == NXC_ERR_STATE, "final twice");

    CHECK(nxc_hash_init(&hc, (nxc_hash_alg_t)99) == NXC_ERR_ARG, "unknown alg");
    CHECK(nxc_hash_update(&hc, "x", 1) == NXC_ERR_STATE, "update on failed init");
    nxc_hash_init(&hc, NXC_HASH_SHA384);
    CHECK(nxc_hash_final(&hc, out, 47) == NXC_ERR_LENGTH, "short output buffer");

    CHECK(nxc_hmac(NXC_HASH_SHA256, NULL, 4, "x", 1, out, 64) == NXC_ERR_ARG, "hmac NULL key");
    CHECK(nxc_hmac(NXC_HASH_SHA256, NULL, 0, "x", 1, out, 31) == NXC_ERR_LENGTH, "hmac short out");

    uint8_t prk[32] = {0}, okm[16];
    CHECK(nxc_hkdf_expand(NXC_HASH_SHA256, prk, 31, NULL, 0, okm, 16) == NXC_ERR_LENGTH, "short prk");
    CHECK(nxc_hkdf_expand(NXC_HASH_SHA256, prk, 32, NULL, 0, okm, 255 * 32 + 1) == NXC_ERR_LENGTH,
          "okm > 255 blocks");

    uint8_t a[4] = {1, 2, 3, 4}, b[4] = {1, 2, 3, 5};
    CHECK(nxc_ct_equal(a, a, 4) == 1 && nxc_ct_equal(a, b, 4) == 0 &&
          nxc_ct_equal(a, b, 3) == 1, "ct_equal");
}

int main(int argc, char **argv) {
    if (argc != 2) {
        printf("usage: %s <vector dir from crypto_fetch_vectors.sh>\n", argv[0]);
        return 2;
    }
    const char *dir = argv[1];

    run_msg_file(dir, NXC_HASH_SHA256, "hashes/SHA2/SHA256ShortMsg.rsp");
    run_msg_file(dir, NXC_HASH_SHA256, "hashes/SHA2/SHA256LongMsg.rsp");
    run_msg_file(dir, NXC_HASH_SHA384, "hashes/SHA2/SHA384ShortMsg.rsp");
    run_msg_file(dir, NXC_HASH_SHA384, "hashes/SHA2/SHA384LongMsg.rsp");
    run_msg_file(dir, NXC_HASH_SHA512, "hashes/SHA2/SHA512ShortMsg.rsp");
    run_msg_file(dir, NXC_HASH_SHA512, "hashes/SHA2/SHA512LongMsg.rsp");
    run_monte(dir, NXC_HASH_SHA256, "hashes/SHA2/SHA256Monte.rsp");
    run_monte(dir, NXC_HASH_SHA384, "hashes/SHA2/SHA384Monte.rsp");
    run_monte(dir, NXC_HASH_SHA512, "hashes/SHA2/SHA512Monte.rsp");
    run_hmac(dir, NXC_HASH_SHA256, "HMAC/rfc-4231-sha256.txt");
    run_hmac(dir, NXC_HASH_SHA384, "HMAC/rfc-4231-sha384.txt");
    run_hmac(dir, NXC_HASH_SHA512, "HMAC/rfc-4231-sha512.txt");
    run_hkdf(dir);
    test_streaming();
    test_errors();
    return vec_report("crypto_sha2_test");
}
