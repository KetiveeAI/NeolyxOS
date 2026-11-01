/*
 * NeolyxOS cryptotest - runs lib/nxcrypto inside NeolyxOS.
 *
 * The full published vector suites run on the build host
 * (make -C lib/nxcrypto test). This program checks that the same
 * freestanding library behaves identically on NeolyxOS itself, with
 * known answers taken from those published sets:
 *
 *   SHA-256, SHA-384, SHA-512 of the empty message (NIST CAVP ShortMsg)
 *   HMAC-SHA-256 RFC 4231 test case 2, HKDF-SHA-256 RFC 5869 A.1
 *   X25519 RFC 7748 section 6.1 (both sides of the exchange)
 *   Ed25519 RFC 8032 section 7.1 TEST 2 (key, signature, verify) and
 *   rejection of a tampered message
 *   ChaCha20-Poly1305 RFC 8439 section 2.8.2, AES-128-GCM (NIST CAVP
 *   gcmEncryptExtIV128, PTlen 408 / AADlen 160 / Taglen 128, Count 0),
 *   and rejection of a flipped tag bit for both
 *   ECDSA P-256/SHA-256, ECDSA P-384/SHA-384, RSA-2048 PKCS#1 v1.5 and
 *   PSS verify (Project Wycheproof valid cases), and rejection of a
 *   tampered hash or signature
 *   X.509 path validation (lib/nxtls) of a real server chain from
 *   x509-limbo, and refusal of an expired certificate, a wrong host, an
 *   untrusted root and a missing intermediate
 *   TLS 1.3 key schedule and record protection (lib/nxtls), replaying
 *   the RFC 8448 section 3 handshake: the server's records must decrypt
 *   to the recorded messages and the client's records must come out
 *   byte for byte as recorded; a tampered record must be refused
 *   TLS 1.3 client (lib/nxtls nxt_tls_connect) against canned server
 *   answers: the ClientHello it sends, and refusal with the right alert
 *   of a TLS 1.2 server and of a ServerHello that does not echo the
 *   session id (the full handshake is tested on the host and by fetch)
 *
 * It also times Ed25519, X25519, the two ciphers, signature
 * verification, chain validation and the TLS key schedule in the VM.
 * Results go to the serial log as "[CRYPTOTEST] PASS/FAIL ..." ending
 * with "[CRYPTOTEST] RESULT: PASS" or "[CRYPTOTEST] RESULT: FAIL".
 * Exit code 0 on PASS, 1 on FAIL.
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>
#include "nxcrypto.h"
#include "nxtls/x509_verify.h"
#include "nxtls/tls.h"
#include "x509_fixture.h"
#include "tls_fixture.h"
/* The TLS key schedule and record layer are internal to lib/nxtls; this
 * test calls them directly, as the host test does. */
#include "../../../lib/nxtls/src/tls13.h"

#define SYS_EXIT        1
#define SYS_TIME        3
#define SYS_DEBUG_PRINT 127

static inline int64_t sc1(int64_t num, int64_t a1) {
    int64_t ret;
    __asm__ volatile("syscall" : "=a"(ret) : "a"(num), "D"(a1) : "rcx", "r11", "memory");
    return ret;
}

static char g_line[200];
static int g_len;
static int g_fail;

static void out_str(const char *s) {
    while (*s && g_len < (int)sizeof(g_line) - 2) g_line[g_len++] = *s++;
}

static void out_dec(uint64_t u) {
    char buf[21];
    int i = 20;
    buf[i] = '\0';
    if (u == 0) buf[--i] = '0';
    while (u > 0 && i > 0) { buf[--i] = (char)('0' + u % 10); u /= 10; }
    out_str(&buf[i]);
}

static void flush(void) {
    g_line[g_len++] = '\n';
    g_line[g_len] = '\0';
    sc1(SYS_DEBUG_PRINT, (int64_t)g_line);
    g_len = 0;
}

static uint64_t now_ms(void) {
    return (uint64_t)sc1(SYS_TIME, 0);
}

static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

/* Decode exactly n bytes of lowercase hex; returns 0 on success. */
static int unhex(const char *h, uint8_t *out, int n) {
    for (int i = 0; i < n; i++) {
        int hi = hexval(h[2 * i]), lo = hexval(h[2 * i + 1]);
        if (hi < 0 || lo < 0) return -1;
        out[i] = (uint8_t)(hi << 4 | lo);
    }
    return h[2 * n] == '\0' ? 0 : -1;
}

static int same(const uint8_t *a, const uint8_t *b, int n) {
    return nxc_ct_equal(a, b, (size_t)n);
}

static void report(const char *name, int ok) {
    out_str("[CRYPTOTEST] ");
    out_str(ok ? "PASS " : "FAIL ");
    out_str(name);
    flush();
    if (!ok) g_fail = 1;
}

/* Compare a computed value with the expected hex string. */
static void expect(const char *name, int rc, const uint8_t *got, const char *hex, int n) {
    uint8_t want[64];
    int ok = rc == NXC_OK && n <= 64 && unhex(hex, want, n) == 0 && same(got, want, n);
    report(name, ok);
}

static void test_hashes(void) {
    uint8_t d[64];
    expect("SHA-256 empty", nxc_sha256(NULL, 0, d),
           d, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", 32);
    expect("SHA-384 empty", nxc_sha384(NULL, 0, d), d,
           "38b060a751ac96384cd9327eb1b1e36a21fdb71114be07434c0cc7bf63f6e1da"
           "274edebfe76f65fbd51ad2f14898b95b", 48);
    expect("SHA-512 empty", nxc_sha512(NULL, 0, d), d,
           "cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce"
           "47d0d13c5d85f2b0ff8318d2877eec2f63b931bd47417a81a538327af927da3e", 64);

    static const char key[] = "Jefe";
    static const char msg[] = "what do ya want for nothing?";
    expect("HMAC-SHA-256 RFC 4231 case 2",
           nxc_hmac(NXC_HASH_SHA256, key, 4, msg, sizeof(msg) - 1, d, sizeof(d)), d,
           "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843", 32);

    uint8_t ikm[22], salt[13], info[10], prk[32], okm[42];
    for (int i = 0; i < 22; i++) ikm[i] = 0x0b;
    for (int i = 0; i < 13; i++) salt[i] = (uint8_t)i;
    for (int i = 0; i < 10; i++) info[i] = (uint8_t)(0xf0 + i);
    int rc = nxc_hkdf_extract(NXC_HASH_SHA256, salt, 13, ikm, 22, prk, 32);
    expect("HKDF-SHA-256 RFC 5869 A.1 PRK", rc, prk,
           "077709362c2e32df0ddc3f0dc47bba6390b6c73bb50f9c3122ec844ad7c2b3e5", 32);
    rc = nxc_hkdf_expand(NXC_HASH_SHA256, prk, 32, info, 10, okm, 42);
    uint8_t want[42];
    int ok = rc == NXC_OK &&
             unhex("3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf"
                   "34007208d5b887185865", want, 42) == 0 && same(okm, want, 42);
    report("HKDF-SHA-256 RFC 5869 A.1 OKM", ok);
}

static void test_x25519(void) {
    uint8_t a[32], b[32], apub[32], bpub[32], s1[32], s2[32];
    unhex("77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a", a, 32);
    unhex("5dab087e624a8a4b79e17f8b83800ee66f3bb1292618b6fd1c2f8b27ff88e0eb", b, 32);
    expect("X25519 RFC 7748 6.1 Alice public", nxc_x25519_public(apub, a), apub,
           "8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a", 32);
    expect("X25519 RFC 7748 6.1 Bob public", nxc_x25519_public(bpub, b), bpub,
           "de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f", 32);

    uint64_t t0 = now_ms();
    int rc = nxc_x25519(s1, a, bpub);
    uint64_t t1 = now_ms();
    expect("X25519 RFC 7748 6.1 shared (Alice)", rc, s1,
           "4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742", 32);
    expect("X25519 RFC 7748 6.1 shared (Bob)", nxc_x25519(s2, b, apub), s2,
           "4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742", 32);

    uint8_t zero[32] = { 0 };
    report("X25519 low-order point refused", nxc_x25519(s2, a, zero) == NXC_ERR_WEAK);

    out_str("[CRYPTOTEST] time X25519 one exchange: ");
    out_dec(t1 - t0);
    out_str(" ms");
    flush();
}

static void test_ed25519(void) {
    /* RFC 8032 section 7.1 TEST 2: one-byte message 0x72. */
    uint8_t seed[32], pub[32], sig[64];
    uint8_t msg[1] = { 0x72 };
    unhex("4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb", seed, 32);
    expect("Ed25519 RFC 8032 TEST 2 public key", nxc_ed25519_public(pub, seed), pub,
           "3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c", 32);

    uint64_t t0 = now_ms();
    int rc = nxc_ed25519_sign(sig, msg, 1, seed);
    uint64_t t1 = now_ms();
    expect("Ed25519 RFC 8032 TEST 2 signature", rc, sig,
           "92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da"
           "085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00", 64);

    uint64_t t2 = now_ms();
    rc = nxc_ed25519_verify(sig, msg, 1, pub);
    uint64_t t3 = now_ms();
    report("Ed25519 RFC 8032 TEST 2 verify", rc == NXC_OK);

    msg[0] ^= 1;
    report("Ed25519 tampered message refused", nxc_ed25519_verify(sig, msg, 1, pub) == NXC_ERR_VERIFY);

    out_str("[CRYPTOTEST] time Ed25519 sign: ");
    out_dec(t1 - t0);
    out_str(" ms, verify: ");
    out_dec(t3 - t2);
    out_str(" ms");
    flush();
}

/* Bulk-throughput sample: seal 64 KiB `rounds` times, report KiB/s. */
static uint8_t g_bulk[65536];

static void time_cipher(const char *name, int which, const nxc_gcm_ctx *g, const uint8_t *key) {
    uint8_t nonce[12] = { 0 }, tag[16];
    int rounds = which == 0 ? 32 : 2;
    uint64_t t0 = now_ms();
    for (int i = 0; i < rounds; i++) {
        nonce[0] = (uint8_t)i;
        if (which == 0)
            nxc_chacha20poly1305_seal(g_bulk, tag, key, nonce, NULL, 0, g_bulk, sizeof(g_bulk));
        else
            nxc_gcm_seal(g, g_bulk, tag, nonce, NULL, 0, g_bulk, sizeof(g_bulk));
    }
    uint64_t ms = now_ms() - t0;
    out_str("[CRYPTOTEST] time ");
    out_str(name);
    out_str(": ");
    out_dec(ms ? (uint64_t)rounds * 64 * 1000 / ms : 0);
    out_str(" KiB/s");
    flush();
}

static void test_aead(void) {
    static const char pt_text[] = "Ladies and Gentlemen of the class of '99: If I could offer you "
                                  "only one tip for the future, sunscreen would be it.";
    uint8_t key[32], nonce[12], aad[20], ct[114], out[114], tag[16];
    unhex("808182838485868788898a8b8c8d8e8f909192939495969798999a9b9c9d9e9f", key, 32);
    unhex("070000004041424344454647", nonce, 12);
    unhex("50515253c0c1c2c3c4c5c6c7", aad, 12);
    int rc = nxc_chacha20poly1305_seal(ct, tag, key, nonce, aad, 12,
                                       (const uint8_t *)pt_text, 114);
    uint8_t want[114];
    int ok = rc == NXC_OK &&
             unhex("d31a8d34648e60db7b86afbc53ef7ec2a4aded51296e08fea9e2b5a736ee62d6"
                   "3dbea45e8ca9671282fafb69da92728b1a71de0a9e060b2905d6a5b67ecd3b36"
                   "92ddbd7f2d778b8c9803aee328091b58fab324e4fad675945585808b4831d7bc"
                   "3ff4def08e4b7a9de576d26586cec64b6116", want, 114) == 0 &&
             same(ct, want, 114);
    report("ChaCha20-Poly1305 RFC 8439 2.8.2 ciphertext", ok);
    expect("ChaCha20-Poly1305 RFC 8439 2.8.2 tag", rc, tag, "1ae10b594f09e26a7e902ecbd0600691", 16);
    rc = nxc_chacha20poly1305_open(out, key, nonce, aad, 12, ct, 114, tag);
    report("ChaCha20-Poly1305 open", rc == NXC_OK && same(out, (const uint8_t *)pt_text, 114));
    tag[5] ^= 0x20;
    report("ChaCha20-Poly1305 flipped tag refused",
           nxc_chacha20poly1305_open(out, key, nonce, aad, 12, ct, 114, tag) == NXC_ERR_VERIFY);

    uint8_t gk[16], giv[12], gpt[51], gct[51];
    nxc_gcm_ctx g;
    unhex("fe47fcce5fc32665d2ae399e4eec72ba", gk, 16);
    unhex("5adb9609dbaeb58cbd6e7275", giv, 12);
    unhex("7c0e88c88899a779228465074797cd4c2e1498d259b54390b85e3eef1c02df60"
          "e743f1b840382c4bccaf3bafb4ca8429bea063", gpt, 51);
    unhex("88319d6e1d3ffa5f987199166c8a9b56c2aeba5a", aad, 20);
    rc = nxc_gcm_init(&g, gk, 16);
    if (rc == NXC_OK) rc = nxc_gcm_seal(&g, gct, tag, giv, aad, 20, gpt, 51);
    uint8_t gwant[51];
    ok = rc == NXC_OK &&
         unhex("98f4826f05a265e6dd2be82db241c0fbbbf9ffb1c173aa83964b7cf539304373"
               "6365253ddbc5db8778371495da76d269e5db3e", gwant, 51) == 0 && same(gct, gwant, 51);
    report("AES-128-GCM CAVP ciphertext", ok);
    expect("AES-128-GCM CAVP tag", rc, tag, "291ef1982e4defedaa2249f898556b47", 16);
    tag[0] ^= 1;
    report("AES-128-GCM flipped tag refused",
           nxc_gcm_open(&g, out, giv, aad, 20, gct, 51, tag) == NXC_ERR_VERIFY);

    time_cipher("ChaCha20-Poly1305", 0, &g, key);
    time_cipher("AES-128-GCM", 1, &g, key);
    nxc_gcm_wipe(&g);
}

/* Print "[CRYPTOTEST] time <name>: <ms> ms". */
static void report_time(const char *name, uint64_t ms) {
    out_str("[CRYPTOTEST] time ");
    out_str(name);
    out_str(": ");
    out_dec(ms);
    out_str(" ms");
    flush();
}

/*
 * Signature verification with valid cases from Project Wycheproof
 * (testvectors_v1, tcId 2 of each file): ECDSA P-256/SHA-256 and
 * P-384/SHA-384 over "Msg", and RSA-2048 PKCS#1 v1.5 and PSS (SHA-256,
 * MGF1, 32-byte salt) over 20 zero bytes, all with the same key.
 */
static void test_pubkey(void) {
    static uint8_t pub[97], sig[256], n[256];
    static const uint8_t e[3] = { 0x01, 0x00, 0x01 };
    uint8_t h[48], msg[20] = { 0 };
    const uint8_t ecmsg[3] = { 'M', 's', 'g' };

    int ok = unhex("0404aaec73635726f213fb8a9e64da3b8632e41495a944d0045b522eba7240fa"
                    "d587d9315798aaa3a5ba01775787ced05eaaf7b4e09fc81d6d1aa546e8365d52"
                    "5d", pub, 65) == 0 &&
             unhex("30450220530bd6b0c9af2d69ba897f6b5fb59695cfbf33afe66dbadcf5b8d2a2"
                    "a6538e23022100d85e489cb7a161fd55ededcedbf4cc0c0987e3e3f0f242cae9"
                    "34c72caa3f43e9", sig, 71) == 0 &&
             nxc_sha256(ecmsg, 3, h) == NXC_OK;
    uint64_t t0 = now_ms();
    int rc = ok ? nxc_ecdsa_verify_der(NXC_EC_P256, pub, 65, h, 32, sig, 71) : NXC_ERR_ARG;
    uint64_t t1 = now_ms();
    report("ECDSA P-256 SHA-256 verify", rc == NXC_OK);
    h[0] ^= 1;
    report("ECDSA P-256 tampered hash refused",
           nxc_ecdsa_verify_der(NXC_EC_P256, pub, 65, h, 32, sig, 71) == NXC_ERR_VERIFY);
    report_time("ECDSA P-256 verify", t1 - t0);

    ok = unhex("0429bdb76d5fa741bfd70233cb3a66cc7d44beb3b0663d92a8136650478bcefb"
                "61ef182e155a54345a5e8e5e88f064e5bc9a525ab7f764dad3dae1468c2b419f"
                "3b62b9ba917d5e8c4fb1ec47404a3fc76474b2713081be9db4c00e043ada9fc4"
                "a3", pub, 97) == 0 &&
         unhex("3066023100d7143a836608b25599a7f28dec6635494c2992ad1e2bbeecb7ef60"
                "1a9c01746e710ce0d9c48accb38a79ede5b9638f3402310080f9e165e8c61035"
                "bf8aa7b5533960e46dd0e211c904a064edb6de41f797c0eae4e327612ee3f816"
                "f4157272bb4fabc9", sig, 104) == 0 &&
         nxc_sha384(ecmsg, 3, h) == NXC_OK;
    t0 = now_ms();
    rc = ok ? nxc_ecdsa_verify_der(NXC_EC_P384, pub, 97, h, 48, sig, 104) : NXC_ERR_ARG;
    t1 = now_ms();
    report("ECDSA P-384 SHA-384 verify", rc == NXC_OK);
    report_time("ECDSA P-384 verify", t1 - t0);

    ok = unhex("a2b451a07d0aa5f96e455671513550514a8a5b462ebef717094fa1fee82224e6"
                "37f9746d3f7cafd31878d80325b6ef5a1700f65903b469429e89d6eac8845097"
                "b5ab393189db92512ed8a7711a1253facd20f79c15e8247f3d3e42e46e48c98e"
                "254a2fe9765313a03eff8f17e1a029397a1fa26a8dce26f490ed81299615d981"
                "4c22da610428e09c7d9658594266f5c021d0fceca08d945a12be82de4d1ece6b"
                "4c03145b5d3495d4ed5411eb878daf05fd7afc3e09ada0f1126422f590975a19"
                "69816f48698bcbba1b4d9cae79d460d8f9f85e7975005d9bc22c4e5ac0f7c1a4"
                "5d12569a62807d3b9a02e5a530e773066f453d1f5b4c2e9cf7820283f742b9d5", n, 256) == 0 &&
         unhex("8a1b220cb2ab415dc760eb7f5bb10335a3cca269d7dbbf7d0962ba79f9cf7b43"
                "a5fc09c99a1584f07403473d6c189a836897a5b6f8ea9fa22d601e6ba5f7411f"
                "e27c638b81b1a22363583a80fce8c7df3e40fb51bd0e60d0a6653f79f3bcb7ec"
                "3e9dc14cfb5b31ab1735bca692d50ac03f979dda92747c6430f8045efa3513ba"
                "6e0ce3e9e35570e1c30c8ebe589b44192e1344ca83dfa576fc6fdc7bf1cd7cee"
                "875b001c8c02ce8d602769e4bd9d241c4857182a0089a8b67644e73eef105c55"
                "0efa47a40874289395ac0c4e02fd4ba98e130a4c2d1b95521c6af4a002ac3bdc"
                "6e52122ae4c08cc3da1c896e059acbddec574ac0432f6103dd97273d8803c102", sig, 256) == 0 &&
         nxc_sha256(msg, 20, h) == NXC_OK;
    t0 = now_ms();
    rc = ok ? nxc_rsa_verify_pkcs1v15(n, 256, e, 3, NXC_HASH_SHA256, h, 32, sig, 256) : NXC_ERR_ARG;
    t1 = now_ms();
    report("RSA-2048 PKCS#1 v1.5 SHA-256 verify", rc == NXC_OK);
    sig[100] ^= 0x10;
    report("RSA-2048 PKCS#1 v1.5 tampered signature refused",
           nxc_rsa_verify_pkcs1v15(n, 256, e, 3, NXC_HASH_SHA256, h, 32, sig, 256) == NXC_ERR_VERIFY);
    report_time("RSA-2048 verify", t1 - t0);

    ok = unhex("0658c68fe0895646056d9bca422a64fe48813b4e14f0c8c4122e56d345b6813d"
                "c6286ffde014617e351c7af0a0d2c0f285def79cb734e1e055a25fa6fddc1c07"
                "da17b4b235c637413b1849c24311fa72331f4c0458c364a4916de8619b884d7e"
                "37288fad12926fc091f4851686a04fd0a504dbce3db370663a6ea6128fea86c2"
                "ca94c63e0d34d7f2c845b5d71d9a5e544451f524a451acb85c49bba7864e0a34"
                "a48613a819caf3dfd0d510c940f1df21c3373915be1f3509a557fa4d5a4e9f27"
                "3e85467961133e2482c0907386454228fb0246638616fc31bbb6fa7c2361b803"
                "5994eec69a923f4c0bb0ba8696dfe8b1400c2398d7b343fdf498b1116c8de602", sig, 256) == 0;
    rc = ok ? nxc_rsa_verify_pss(n, 256, e, 3, NXC_HASH_SHA256, h, 32, 32, sig, 256) : NXC_ERR_ARG;
    report("RSA-2048 PSS SHA-256 verify", rc == NXC_OK);
    h[31] ^= 1;
    report("RSA-2048 PSS tampered hash refused",
           nxc_rsa_verify_pss(n, 256, e, 3, NXC_HASH_SHA256, h, 32, 32, sig, 256) == NXC_ERR_VERIFY);
}

/*
 * X.509 path validation (lib/nxtls) on a real chain: the fastly.com
 * server certificate, its intermediate and its root, from x509-limbo.
 * Also the refusals HTTPS depends on: expired, self-signed (no trusted
 * root) and wrong host. Each refusal prints its reason, as the TLS
 * client will log it.
 */
static void x509_case(const char *name, int want, const nxt_x509_cert *chain, size_t n,
                      const nxt_trust_store *ts, int64_t now, const char *host) {
    nxt_verify_opts o = { now, host, 0 };
    nxt_verify_result r;
    int rc = nxt_x509_verify_server(chain, n, ts, &o, &r);
    report(name, rc == want);
    out_str("[CRYPTOTEST]   -> ");
    out_str(nxt_x509_err_str(rc));
    flush();
}

static void test_x509(void) {
    static nxt_x509_cert chain[2], root;
    int ok = nxt_x509_parse(&chain[0], FIXTURE_LEAF, sizeof(FIXTURE_LEAF)) == NXT_X509_OK &&
             nxt_x509_parse(&chain[1], FIXTURE_INTERMEDIATE, sizeof(FIXTURE_INTERMEDIATE)) == NXT_X509_OK &&
             nxt_x509_parse(&root, FIXTURE_ROOT, sizeof(FIXTURE_ROOT)) == NXT_X509_OK;
    report("X.509 parse leaf, intermediate, root", ok);
    if (!ok) return;
    nxt_trust_store ts = { &root, 1 };
    nxt_trust_store empty = { &root, 0 };

    uint64_t t0 = now_ms();
    x509_case("X.509 chain for " FIXTURE_HOST " verifies", NXT_X509_OK, chain, 2, &ts,
              FIXTURE_TIME, FIXTURE_HOST);
    uint64_t t1 = now_ms();
    x509_case("X.509 expired certificate refused", NXT_X509_ERR_EXPIRED, chain, 2, &ts,
              FIXTURE_TIME + 90LL * 86400, FIXTURE_HOST);
    x509_case("X.509 wrong host refused", NXT_X509_ERR_HOST_MISMATCH, chain, 2, &ts,
              FIXTURE_TIME, "evil.example.com");
    x509_case("X.509 chain to an untrusted root refused", NXT_X509_ERR_UNKNOWN_ISSUER, chain, 2,
              &empty, FIXTURE_TIME, FIXTURE_HOST);
    x509_case("X.509 missing intermediate refused", NXT_X509_ERR_UNKNOWN_ISSUER, chain, 1, &ts,
              FIXTURE_TIME, FIXTURE_HOST);
    report_time("X.509 chain verify", t1 - t0);
}

/*
 * TLS 1.3 key schedule and record protection on the RFC 8448 section 3
 * handshake. The ClientHello/ServerHello and the client's X25519 key
 * come from the trace; everything after that is computed here.
 */
static uint8_t g_rec[NXT_TLS_MAX_RECORD];

static size_t load_rec(const uint8_t *src, size_t n) {
    for (size_t i = 0; i < n && i < sizeof(g_rec); i++) g_rec[i] = src[i];
    return n <= sizeof(g_rec) ? n : 0;
}

static void test_tls13(void) {
    const nxt__suite *suite = nxt__suite_find(TLS_FIXTURE_SUITE);
    report("TLS 1.3 suite from the trace is supported", suite != NULL);
    if (!suite) return;
    uint64_t t0 = now_ms();

    uint8_t shared[32], th[NXT__MAX_HASH];
    nxc_hash_ctx h;
    static nxt__keysched ks;
    int ok = nxc_x25519(shared, TLS_CLIENT_PRIV, TLS_SERVER_PUB) == NXC_OK &&
             nxc_hash_init(&h, suite->hash) == NXC_OK &&
             nxc_hash_update(&h, TLS_CLIENT_HELLO, sizeof(TLS_CLIENT_HELLO)) == NXC_OK &&
             nxc_hash_update(&h, TLS_SERVER_HELLO, sizeof(TLS_SERVER_HELLO)) == NXC_OK;
    nxc_hash_ctx h2 = h;
    ok = ok && nxc_hash_final(&h2, th, sizeof(th)) == NXC_OK &&
         nxt__ks_init(&ks, suite) == NXT_TLS_OK &&
         nxt__ks_handshake(&ks, shared, sizeof(shared), th) == NXT_TLS_OK;
    report("TLS 1.3 handshake secrets", ok);
    if (!ok) return;

    /* Server flight: one protected record holding EE, Certificate, CV, Finished. */
    static nxt__rprot srv, cli;
    uint8_t type = 0;
    size_t len = 0, n = load_rec(TLS_SERVER_FLIGHT_REC, sizeof(TLS_SERVER_FLIGHT_REC));
    ok = nxt__rprot_init(&srv, suite, ks.server_hs) == NXT_TLS_OK &&
         nxt__rprot_init(&cli, suite, ks.client_hs) == NXT_TLS_OK &&
         nxt__rprot_open(&srv, g_rec, n, &type, &len) == NXT_TLS_OK && type == 22 &&
         len == sizeof(TLS_SERVER_MSGS) + sizeof(TLS_SERVER_FINISHED) &&
         same(g_rec + 5, TLS_SERVER_MSGS, (int)sizeof(TLS_SERVER_MSGS)) &&
         same(g_rec + 5 + sizeof(TLS_SERVER_MSGS), TLS_SERVER_FINISHED,
              (int)sizeof(TLS_SERVER_FINISHED));
    report("TLS 1.3 server handshake record decrypts to the recorded messages", ok);
    if (!ok) return;

    /* Server Finished over ClientHello..CertificateVerify. */
    uint8_t mac[NXT__MAX_HASH];
    h2 = h;
    ok = nxc_hash_update(&h2, TLS_SERVER_MSGS, sizeof(TLS_SERVER_MSGS)) == NXC_OK &&
         nxc_hash_final(&h2, th, sizeof(th)) == NXC_OK &&
         nxt__finished_mac(suite, ks.server_hs, th, mac) == NXT_TLS_OK &&
         same(mac, TLS_SERVER_FINISHED + 4, suite->hash_len);
    report("TLS 1.3 server Finished verifies", ok);

    /* Client Finished over ClientHello..server Finished, sealed as recorded. */
    uint8_t cfin[4 + NXT__MAX_HASH] = { 20, 0, 0, (uint8_t)suite->hash_len };
    nxc_hash_update(&h, TLS_SERVER_MSGS, sizeof(TLS_SERVER_MSGS));
    nxc_hash_update(&h, TLS_SERVER_FINISHED, sizeof(TLS_SERVER_FINISHED));
    ok = nxc_hash_final(&h, th, sizeof(th)) == NXC_OK &&
         nxt__finished_mac(suite, ks.client_hs, th, cfin + 4) == NXT_TLS_OK &&
         nxt__rprot_seal(&cli, 22, cfin, 4 + suite->hash_len, 0, g_rec, sizeof(g_rec), &len)
             == NXT_TLS_OK &&
         len == sizeof(TLS_CLIENT_FINISHED_REC) &&
         same(g_rec, TLS_CLIENT_FINISHED_REC, (int)len);
    report("TLS 1.3 client Finished record matches RFC 8448", ok);

    /* Application traffic. */
    nxt__rprot_wipe(&srv);
    nxt__rprot_wipe(&cli);
    ok = nxt__ks_application(&ks, th) == NXT_TLS_OK &&
         nxt__rprot_init(&srv, suite, ks.server_ap) == NXT_TLS_OK &&
         nxt__rprot_init(&cli, suite, ks.client_ap) == NXT_TLS_OK;
    uint64_t t1 = now_ms();
    n = load_rec(TLS_NST_REC, sizeof(TLS_NST_REC));
    ok = ok && nxt__rprot_open(&srv, g_rec, n, &type, &len) == NXT_TLS_OK && type == 22 &&
         g_rec[5] == 4;
    n = load_rec(TLS_SERVER_APP_REC, sizeof(TLS_SERVER_APP_REC));
    ok = ok && nxt__rprot_open(&srv, g_rec, n, &type, &len) == NXT_TLS_OK && type == 23 &&
         len == sizeof(TLS_SERVER_APP) && same(g_rec + 5, TLS_SERVER_APP, (int)len);
    report("TLS 1.3 server ticket and application data decrypt", ok);
    ok = nxt__rprot_seal(&cli, 23, TLS_CLIENT_APP, sizeof(TLS_CLIENT_APP), 0, g_rec,
                         sizeof(g_rec), &len) == NXT_TLS_OK &&
         len == sizeof(TLS_CLIENT_APP_REC) && same(g_rec, TLS_CLIENT_APP_REC, (int)len);
    report("TLS 1.3 client application data record matches RFC 8448", ok);

    /* A record with one flipped bit is refused and wiped. */
    nxt__rprot r2;
    for (size_t i = 0; i < sizeof(r2); i++) ((uint8_t *)&r2)[i] = 0;
    n = load_rec(TLS_SERVER_APP_REC, sizeof(TLS_SERVER_APP_REC));
    g_rec[n - 1] ^= 0x01;
    ok = nxt__rprot_init(&r2, suite, ks.server_ap) == NXT_TLS_OK &&
         nxt__rprot_open(&r2, g_rec, n, &type, &len) == NXT_TLS_ERR_BAD_RECORD_MAC;
    report("TLS 1.3 tampered record refused", ok);

    nxt__rprot_wipe(&r2);
    nxt__rprot_wipe(&srv);
    nxt__rprot_wipe(&cli);
    nxt__ks_wipe(&ks);
    nxc_memzero(shared, sizeof(shared));
    report_time("TLS 1.3 key schedule (X25519 + HKDF + 2 records)", t1 - t0);
}

/*
 * TLS 1.3 client smoke test: nxt_tls_connect() over a canned transport.
 * What the client sends is kept in g_tx; recv hands out one server record.
 */
static uint8_t g_tx[2048];
static size_t g_tx_len;
static const uint8_t *g_srv;
static size_t g_srv_len;

static long smoke_send(void *ctx, const uint8_t *d, size_t n) {
    (void)ctx;
    for (size_t i = 0; i < n && g_tx_len < sizeof(g_tx); i++) g_tx[g_tx_len++] = d[i];
    return (long)n;
}

static long smoke_recv(void *ctx, uint8_t *b, size_t cap) {
    (void)ctx;
    size_t k = g_srv_len < cap ? g_srv_len : cap;
    for (size_t i = 0; i < k; i++) b[i] = g_srv[i];
    g_srv += k;
    g_srv_len -= k;
    return (long)k;
}

/* Deterministic bytes: this test checks behaviour, not secrecy. */
static int smoke_random(void *ctx, uint8_t *out, size_t len) {
    unsigned *ctr = ctx;
    for (size_t i = 0; i < len; i++) out[i] = (uint8_t)(*ctr * 131u + i * 7u + 1u);
    (*ctr)++;
    return 0;
}

static int has_bytes(const uint8_t *hay, size_t n, const char *needle) {
    size_t k = 0;
    while (needle[k]) k++;
    for (size_t i = 0; i + k <= n; i++) {
        size_t j = 0;
        while (j < k && hay[i + j] == (uint8_t)needle[j]) j++;
        if (j == k) return 1;
    }
    return 0;
}

static nxt_tls_conn g_tls;
static uint8_t g_sh_rec[5 + sizeof(TLS_SERVER_HELLO)];

static int smoke_connect(const uint8_t *srv, size_t srv_len) {
    static const nxt_trust_store none = { 0, 0 };
    static unsigned ctr;
    nxt_tls_config cfg = { "example.test", &none, 1767225600, { 0, smoke_send, smoke_recv },
                           smoke_random, &ctr, "http/1.1" };
    g_tx_len = 0;
    g_srv = srv;
    g_srv_len = srv_len;
    return nxt_tls_connect(&g_tls, &cfg);
}

/* The last 7 bytes the client sent are a plaintext fatal alert `desc`. */
static int sent_alert(uint8_t desc) {
    static const uint8_t hdr[6] = { 21, 3, 3, 0, 2, 2 };
    if (g_tx_len < 7) return 0;
    return same(g_tx + g_tx_len - 7, hdr, 6) && g_tx[g_tx_len - 1] == desc;
}

static void test_tls_client(void) {
    /* A TLS 1.2 ServerHello (ECDHE-RSA-AES128-GCM-SHA256, renegotiation_info):
     * no supported_versions extension. */
    static const uint8_t tls12[] = {
        22, 3, 3, 0, 49,  2, 0, 0, 45,  3, 3,
        1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
        17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32,
        0,  0xc0, 0x2f,  0,  0, 5,  0xff, 0x01, 0, 1, 0 };
    uint64_t t0 = now_ms();
    int rc = smoke_connect(tls12, sizeof(tls12));
    uint64_t t1 = now_ms();
    const nxt_tls_info *in = nxt_tls_get_info(&g_tls);
    int hello_ok = g_tx_len > 9 && g_tx[0] == 22 && g_tx[1] == 3 && g_tx[2] == 1 && g_tx[5] == 1 &&
                   has_bytes(g_tx, g_tx_len, "example.test") &&
                   has_bytes(g_tx, g_tx_len, "http/1.1");
    report("TLS client sends a ClientHello with SNI and ALPN", hello_ok);
    report("TLS client refuses a TLS 1.2 server with protocol_version",
           rc == NXT_TLS_ERR_PROTOCOL_VERSION && sent_alert(70) && in->alert_sent == 70);

    /* RFC 8448's TLS 1.3 ServerHello: valid, but its session id is empty
     * and ours is 32 bytes, so it must be refused as illegal_parameter. */
    g_sh_rec[0] = 22;
    g_sh_rec[1] = 3;
    g_sh_rec[2] = 3;
    g_sh_rec[3] = (uint8_t)(sizeof(TLS_SERVER_HELLO) >> 8);
    g_sh_rec[4] = (uint8_t)sizeof(TLS_SERVER_HELLO);
    for (size_t i = 0; i < sizeof(TLS_SERVER_HELLO); i++) g_sh_rec[5 + i] = TLS_SERVER_HELLO[i];
    rc = smoke_connect(g_sh_rec, sizeof(g_sh_rec));
    report("TLS client refuses a ServerHello without our session id",
           rc == NXT_TLS_ERR_ILLEGAL_PARAMETER && sent_alert(47));

    /* Nothing from the server at all. */
    rc = smoke_connect(0, 0);
    report("TLS client reports a closed connection", rc == NXT_TLS_ERR_CLOSED);
    nxt_tls_close(&g_tls);
    report_time("TLS ClientHello (X25519 key) and refusal", t1 - t0);
}

void _start(void) {
    out_str("[CRYPTOTEST] start (lib/nxcrypto known-answer tests)");
    flush();
    test_hashes();
    test_x25519();
    test_ed25519();
    test_aead();
    test_pubkey();
    test_x509();
    test_tls13();
    test_tls_client();
    out_str(g_fail ? "[CRYPTOTEST] RESULT: FAIL" : "[CRYPTOTEST] RESULT: PASS");
    flush();
    sc1(SYS_EXIT, g_fail ? 1 : 0);
    for (;;) { __asm__ volatile("pause"); }
}
