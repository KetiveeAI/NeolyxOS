/*
 * Host test for the nxtls TLS 1.3 key schedule and record protection
 * (lib/nxtls/src/tls13_keys.c, tls13_record.c) against the RFC 8448
 * example handshakes.
 *
 * The trace is replayed record by record from the client's side: the
 * key schedule is driven from the recorded ClientHello, ServerHello and
 * the client's X25519 private key, then
 *   - the server's encrypted handshake flight must decrypt to exactly the
 *     recorded EncryptedExtensions..Finished messages,
 *   - the server Finished MAC must verify and the CertificateVerify
 *     content must match the recorded "message to sign",
 *   - the client Finished record built here must equal the recorded one
 *     byte for byte, and so must the application data and close_notify
 *     records in both directions.
 * Secrets, keys and IVs derived here are also looked up in a second,
 * independent transcription of RFC 8448 (Mbed TLS's test data), and the
 * two transcriptions are checked to agree on the application data
 * records. Then the record layer's failure paths (tampering, replay,
 * wrong sequence number, length limits, padding-only records, sequence
 * exhaustion) and all three cipher suites are exercised.
 *
 * Usage: crypto_tls13_trace_test <vector dir>   (see crypto_fetch_vectors.sh)
 *
 * Host-only (uses the C library). Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include "crypto_vec.h"
#include "nxtls/tls.h"
#include "nxcrypto.h"
#include "../../lib/nxtls/src/tls13.h"

/* ------------------------------------------------------------------ */
/* Trace file: "[Section]" headers and "Name = hex" lines              */
/* ------------------------------------------------------------------ */

#define MAX_SECTIONS 16
#define MAX_FIELDS   48

typedef struct {
    char name[64];
    char *key[MAX_FIELDS];
    uint8_t *val[MAX_FIELDS];
    size_t len[MAX_FIELDS];
    int n;
} section;

static section g_sect[MAX_SECTIONS];
static int g_nsect;

/* Hex with optional embedded whitespace (one copy of the trace has a stray space). */
static uint8_t *hex_loose(const char *s, size_t *out_len) {
    size_t n = strlen(s);
    char *clean = malloc(n + 1);
    if (!clean) { perror("malloc"); exit(2); }
    size_t k = 0;
    for (size_t i = 0; i < n; i++)
        if (!isspace((unsigned char)s[i])) clean[k++] = s[i];
    clean[k] = 0;
    uint8_t *b = hex_decode(clean, out_len);
    free(clean);
    return b;
}

static void trace_cb(void *ctx, const char *name, const char *value) {
    (void)ctx;
    if (name[0] == 0) return;
    if (!strcmp(name, "[")) {
        if (g_nsect == MAX_SECTIONS) { printf("ERROR: too many trace sections\n"); exit(2); }
        section *s = &g_sect[g_nsect++];
        snprintf(s->name, sizeof(s->name), "%s", value);
        return;
    }
    if (g_nsect == 0) return;
    section *s = &g_sect[g_nsect - 1];
    if (s->n == MAX_FIELDS) { printf("ERROR: too many fields in %s\n", s->name); exit(2); }
    size_t len = 0;
    uint8_t *v = hex_loose(value, &len);
    /* Fields that are not hex (a timestamp, a PSK identity string) are not
     * used here; need() fails loudly if one that is used is missing. */
    if (!v) return;
    s->key[s->n] = vec_strdup(name);
    s->val[s->n] = v;
    s->len[s->n] = len;
    s->n++;
}

static const section *find_section(const char *name) {
    for (int i = 0; i < g_nsect; i++)
        if (!strcmp(g_sect[i].name, name)) return &g_sect[i];
    printf("ERROR: trace section %s missing\n", name);
    exit(2);
}

/* A field, or NULL if the section does not have it. */
static const uint8_t *tfield(const section *s, const char *name, size_t *len) {
    for (int i = 0; i < s->n; i++)
        if (!strcmp(s->key[i], name)) { *len = s->len[i]; return s->val[i]; }
    *len = 0;
    return NULL;
}

static const uint8_t *need(const section *s, const char *name, size_t *len) {
    const uint8_t *v = tfield(s, name, len);
    if (!v) { printf("ERROR: %s has no %s\n", s->name, name); exit(2); }
    return v;
}

/* ------------------------------------------------------------------ */
/* Cross-check against the second transcription                        */
/* ------------------------------------------------------------------ */

static char *g_xcheck;      /* Mbed TLS test data, lower-cased */

static void tohex(const uint8_t *b, size_t n, char *out) {
    static const char d[] = "0123456789abcdef";
    for (size_t i = 0; i < n; i++) { out[2 * i] = d[b[i] >> 4]; out[2 * i + 1] = d[b[i] & 15]; }
    out[2 * n] = 0;
}

/* Is this value quoted (as a hex string) in the second transcription? */
static int in_xcheck(const uint8_t *b, size_t n) {
    char hex[2 * 256 + 3];
    if (n > 256) return 0;
    hex[0] = '"';
    tohex(b, n, hex + 1);
    hex[2 * n + 1] = '"';
    hex[2 * n + 2] = 0;
    return strstr(g_xcheck, hex) != NULL;
}

/* ------------------------------------------------------------------ */
/* Small helpers                                                        */
/* ------------------------------------------------------------------ */

typedef struct { uint8_t *p; size_t len, cap; } buf;

static void buf_add(buf *b, const uint8_t *p, size_t n) {
    if (b->len + n > b->cap) {
        b->cap = (b->len + n) * 2 + 64;
        b->p = realloc(b->p, b->cap);
        if (!b->p) { perror("realloc"); exit(2); }
    }
    memcpy(b->p + b->len, p, n);
    b->len += n;
}

static void th_of(const nxt__suite *s, const buf *msgs, uint8_t *out) {
    if (nxc_hash(s->hash, msgs->p, msgs->len, out, NXT__MAX_HASH) != NXC_OK) {
        printf("ERROR: hash failed\n");
        exit(2);
    }
}

/* Find extension `type` in a ClientHello or ServerHello body (message
 * header included). Test-only parsing; the client has its own. */
static const uint8_t *hello_ext(const uint8_t *m, size_t n, int client, uint16_t type,
                                size_t *ext_len) {
    size_t i = 4 + 2 + 32;                       /* header, legacy_version, random */
    if (i >= n) return NULL;
    i += 1 + m[i];                               /* legacy_session_id */
    if (client) {
        if (i + 2 > n) return NULL;
        i += 2 + ((size_t)m[i] << 8 | m[i + 1]); /* cipher_suites */
        if (i >= n) return NULL;
        i += 1 + m[i];                           /* legacy_compression_methods */
    } else {
        i += 2 + 1;                              /* cipher_suite, compression */
    }
    if (i + 2 > n) return NULL;
    size_t end = i + 2 + ((size_t)m[i] << 8 | m[i + 1]);
    if (end > n) return NULL;
    i += 2;
    while (i + 4 <= end) {
        uint16_t t = (uint16_t)(m[i] << 8 | m[i + 1]);
        size_t l = (size_t)m[i + 2] << 8 | m[i + 3];
        if (i + 4 + l > end) return NULL;
        if (t == type) { *ext_len = l; return m + i + 4; }
        i += 4 + l;
    }
    return NULL;
}

static uint16_t server_suite(const uint8_t *sh, size_t n) {
    size_t i = 4 + 2 + 32;
    if (i >= n) return 0;
    i += 1 + sh[i];
    if (i + 2 > n) return 0;
    return (uint16_t)(sh[i] << 8 | sh[i + 1]);
}

/* Open every record in a blob of records with p. Change-cipher-spec
 * records (middlebox compatibility mode) must be the single byte 1 and
 * are skipped. The plaintexts are appended to out; every one must have
 * content type want_type. Returns the number of protected records. */
static int open_records(nxt__rprot *p, const uint8_t *blob, size_t n, uint8_t want_type,
                        buf *out, const char *what) {
    int count = 0;
    size_t i = 0;
    while (i < n) {
        CHECK(i + 5 <= n, "%s: truncated record header", what);
        if (i + 5 > n) return count;
        size_t rl = 5 + ((size_t)blob[i + 3] << 8 | blob[i + 4]);
        CHECK(i + rl <= n, "%s: truncated record", what);
        if (i + rl > n) return count;
        if (blob[i] == NXT_TLS_CT_CHANGE_CIPHER_SPEC) {
            CHECK(rl == 6 && blob[i + 5] == 1, "%s: bad change_cipher_spec", what);
        } else {
            uint8_t *rec = malloc(rl);
            memcpy(rec, blob + i, rl);
            uint8_t type = 0;
            size_t len = 0;
            int rc = nxt__rprot_open(p, rec, rl, &type, &len);
            CHECK(rc == NXT_TLS_OK, "%s: open record: %s", what, nxt_tls_err_str(rc));
            CHECK(rc != NXT_TLS_OK || type == want_type, "%s: content type %u", what, type);
            if (rc == NXT_TLS_OK) buf_add(out, rec + 5, len);
            free(rec);
            count++;
        }
        i += rl;
    }
    return count;
}

static int seal_and_compare(nxt__rprot *p, uint8_t type, const uint8_t *data, size_t len,
                            const uint8_t *want, size_t want_len, const char *what) {
    uint8_t out[NXT_TLS_MAX_RECORD];
    size_t out_len = 0;
    int rc = nxt__rprot_seal(p, type, data, len, 0, out, sizeof(out), &out_len);
    CHECK(rc == NXT_TLS_OK, "%s: seal: %s", what, nxt_tls_err_str(rc));
    int same = rc == NXT_TLS_OK && out_len == want_len && memcmp(out, want, want_len) == 0;
    CHECK(same, "%s: record differs from the trace", what);
    return same;
}

/* ------------------------------------------------------------------ */
/* Replay one RFC 8448 handshake                                        */
/* ------------------------------------------------------------------ */

typedef struct {
    nxt__keysched ks;
    const nxt__suite *suite;
} replay_state;

/* server_only: the client's own flight in the trace cannot be rebuilt
 * here (it sends a client certificate, which nxtls does not), so stop
 * after checking the server's flight and Finished. */
static void replay(const char *name, int xcheck, int server_only, replay_state *st) {
    const section *s = find_section(name);
    size_t n;

    /* ClientHello: the record body, as hashed into the transcript. */
    size_t ch_rec_len;
    const uint8_t *ch_rec = need(s, "Record_ClientHello_1", &ch_rec_len);
    const uint8_t *ch = ch_rec + 5;
    size_t ch_len = ch_rec_len - 5;
    size_t sh_len;
    const uint8_t *sh = need(s, "Message_ServerHello", &sh_len);
    size_t shr_len;
    const uint8_t *sh_rec = need(s, "Record_ServerHello", &shr_len);
    CHECK(shr_len == sh_len + 5 && memcmp(sh_rec + 5, sh, sh_len) == 0,
          "%s: ServerHello record and message differ", name);

    const nxt__suite *suite = nxt__suite_find(server_suite(sh, sh_len));
    CHECK(suite != NULL, "%s: unsupported suite in trace", name);
    if (!suite) return;
    st->suite = suite;

    /* The client's random bytes, in the order it drew them: the
     * ClientHello random, the legacy_session_id (32 bytes in
     * compatibility mode, else none), then the X25519 private key. */
    const uint8_t *pool = need(s, "Client_RNG_Pool", &n);
    size_t sid_len = ch[4 + 2 + 32];
    CHECK(sid_len == 0 || sid_len == 32, "%s: session id length %zu", name, sid_len);
    CHECK(n >= 64 + sid_len, "%s: short client RNG pool", name);
    if (n < 64 + sid_len) return;
    CHECK(memcmp(pool, ch + 4 + 2, 32) == 0, "%s: ClientHello random", name);
    CHECK(memcmp(pool + 32, ch + 4 + 2 + 32 + 1, sid_len) == 0, "%s: session id", name);
    const uint8_t *priv = pool + 32 + sid_len;
    uint8_t pub[32], shared[32];
    size_t ks_len = 0, sks_len = 0;
    const uint8_t *cks = hello_ext(ch, ch_len, 1, 0x0033, &ks_len);
    const uint8_t *sks = hello_ext(sh, sh_len, 0, 0x0033, &sks_len);
    CHECK(cks && ks_len == 2 + 4 + 32 && cks[2] == 0x00 && cks[3] == 0x1d,
          "%s: ClientHello key_share is not one X25519 share", name);
    CHECK(sks && sks_len == 4 + 32 && sks[0] == 0x00 && sks[1] == 0x1d,
          "%s: ServerHello key_share is not X25519", name);
    if (!cks || !sks || ks_len != 38 || sks_len != 36) return;
    CHECK(nxc_x25519_public(pub, priv) == NXC_OK && memcmp(pub, cks + 6, 32) == 0,
          "%s: client private key does not match its key share", name);
    CHECK(nxc_x25519(shared, priv, sks + 4) == NXC_OK, "%s: x25519", name);

    /* Handshake secrets from Transcript-Hash(ClientHello, ServerHello). */
    buf tr = {0};
    buf_add(&tr, ch, ch_len);
    buf_add(&tr, sh, sh_len);
    uint8_t th[NXT__MAX_HASH];
    th_of(suite, &tr, th);
    CHECK(nxt__ks_init(&st->ks, suite) == NXT_TLS_OK, "%s: ks_init", name);
    CHECK(nxt__ks_handshake(&st->ks, shared, sizeof(shared), th) == NXT_TLS_OK,
          "%s: ks_handshake", name);
    if (xcheck) {
        CHECK(in_xcheck(st->ks.client_hs, suite->hash_len),
              "%s: client_handshake_traffic_secret not in RFC 8448", name);
        CHECK(in_xcheck(st->ks.server_hs, suite->hash_len),
              "%s: server_handshake_traffic_secret not in RFC 8448", name);
        uint8_t k[32], iv[12];
        CHECK(nxt__hkdf_expand_label(suite->hash, st->ks.client_hs, suite->hash_len, "key",
                                     NULL, 0, k, suite->key_len) == NXT_TLS_OK &&
              in_xcheck(k, suite->key_len), "%s: client handshake key", name);
        CHECK(nxt__hkdf_expand_label(suite->hash, st->ks.client_hs, suite->hash_len, "iv",
                                     NULL, 0, iv, 12) == NXT_TLS_OK && in_xcheck(iv, 12),
              "%s: client handshake iv", name);
        CHECK(nxt__hkdf_expand_label(suite->hash, st->ks.server_hs, suite->hash_len, "key",
                                     NULL, 0, k, suite->key_len) == NXT_TLS_OK &&
              in_xcheck(k, suite->key_len), "%s: server handshake key", name);
        CHECK(nxt__hkdf_expand_label(suite->hash, st->ks.server_hs, suite->hash_len, "iv",
                                     NULL, 0, iv, 12) == NXT_TLS_OK && in_xcheck(iv, 12),
              "%s: server handshake iv", name);
    }

    /* The server's encrypted flight. */
    nxt__rprot srv, cli;
    memset(&srv, 0, sizeof(srv));
    memset(&cli, 0, sizeof(cli));
    CHECK(nxt__rprot_init(&srv, suite, st->ks.server_hs) == NXT_TLS_OK, "%s: srv init", name);
    CHECK(nxt__rprot_init(&cli, suite, st->ks.client_hs) == NXT_TLS_OK, "%s: cli init", name);
    size_t flight_len;
    const uint8_t *flight = need(s, "Record_ServerHandshakeMessages", &flight_len);
    buf plain = {0};
    int nrec = open_records(&srv, flight, flight_len, NXT_TLS_CT_HANDSHAKE, &plain, name);
    CHECK(nrec >= 1, "%s: no protected records in the server flight", name);

    /* It must be exactly EncryptedExtensions [CertificateRequest]
     * Certificate CertificateVerify Finished, as recorded. */
    static const char *const order[] = {
        "Message_EncryptedExtensions", "Message_CertificateRequest",
        "Message_Server_Certificate", "Message_Server_CertificateVerify",
    };
    buf expect = {0};
    for (size_t i = 0; i < sizeof(order) / sizeof(order[0]); i++) {
        const uint8_t *m = tfield(s, order[i], &n);
        if (m) buf_add(&expect, m, n);
    }
    size_t fin_len;
    const uint8_t *fin = need(s, "Message_Server_Finished", &fin_len);
    size_t before_fin = expect.len;
    buf_add(&expect, fin, fin_len);
    CHECK(plain.len == expect.len && memcmp(plain.p, expect.p, expect.len) == 0,
          "%s: decrypted server flight differs from the recorded messages", name);

    /* CertificateVerify content: 64 spaces, context string, 0, Transcript-Hash(..Certificate). */
    size_t cv_len = 0;
    const uint8_t *cv = tfield(s, "Message_Server_CertificateVerify", &cv_len);
    if (cv) {
        buf upto_cert = {0};
        buf_add(&upto_cert, tr.p, tr.len);
        buf_add(&upto_cert, expect.p, before_fin - cv_len);
        uint8_t th_cert[NXT__MAX_HASH];
        th_of(suite, &upto_cert, th_cert);
        static const char ctx[] = "TLS 1.3, server CertificateVerify";
        uint8_t tbs[64 + sizeof(ctx) + NXT__MAX_HASH];
        memset(tbs, 0x20, 64);
        memcpy(tbs + 64, ctx, sizeof(ctx));               /* includes the 0 separator */
        memcpy(tbs + 64 + sizeof(ctx), th_cert, suite->hash_len);
        size_t want_len;
        const uint8_t *want = need(s, "Server_MessageToSign", &want_len);
        CHECK(want_len == 64 + sizeof(ctx) + suite->hash_len &&
              memcmp(tbs, want, want_len) == 0, "%s: CertificateVerify content", name);
        free(upto_cert.p);
    }

    /* Server Finished: HMAC over Transcript-Hash(..CertificateVerify). */
    buf_add(&tr, expect.p, before_fin);
    th_of(suite, &tr, th);
    uint8_t mac[NXT__MAX_HASH];
    CHECK(nxt__finished_mac(suite, st->ks.server_hs, th, mac) == NXT_TLS_OK,
          "%s: finished_mac", name);
    CHECK(fin_len == 4 + (size_t)suite->hash_len && fin[0] == 20 &&
          memcmp(fin + 4, mac, suite->hash_len) == 0, "%s: server Finished MAC", name);
    buf_add(&tr, fin, fin_len);
    if (server_only) {
        nxt__rprot_wipe(&srv);
        nxt__rprot_wipe(&cli);
        free(tr.p);
        free(plain.p);
        free(expect.p);
        printf("  %s: server flight checked\n", name);
        return;
    }

    /* Client Finished: HMAC over Transcript-Hash(..server Finished), sent
     * under the client handshake key. In compatibility mode the recorded
     * flight starts with a change_cipher_spec record. */
    th_of(suite, &tr, th);
    uint8_t cfin[4 + NXT__MAX_HASH] = { 20, 0, 0, (uint8_t)suite->hash_len };
    CHECK(nxt__finished_mac(suite, st->ks.client_hs, th, cfin + 4) == NXT_TLS_OK,
          "%s: client finished_mac", name);
    size_t crec_len;
    const uint8_t *crec = need(s, "Record_ClientFinished", &crec_len);
    static const uint8_t CCS[6] = { 20, 3, 3, 0, 1, 1 };
    if (crec_len > 6 && memcmp(crec, CCS, 6) == 0) { crec += 6; crec_len -= 6; }
    seal_and_compare(&cli, NXT_TLS_CT_HANDSHAKE, cfin, 4 + suite->hash_len,
                     crec, crec_len, name);

    /* Application traffic secrets from the same transcript hash. */
    CHECK(nxt__ks_application(&st->ks, th) == NXT_TLS_OK, "%s: ks_application", name);
    nxt__rprot_wipe(&srv);
    nxt__rprot_wipe(&cli);
    CHECK(nxt__rprot_init(&srv, suite, st->ks.server_ap) == NXT_TLS_OK, "%s: srv ap", name);
    CHECK(nxt__rprot_init(&cli, suite, st->ks.client_ap) == NXT_TLS_OK, "%s: cli ap", name);
    if (xcheck) {
        uint8_t k[32], iv[12];
        CHECK(nxt__hkdf_expand_label(suite->hash, st->ks.client_ap, suite->hash_len, "key",
                                     NULL, 0, k, suite->key_len) == NXT_TLS_OK &&
              in_xcheck(k, suite->key_len), "%s: client application key", name);
        CHECK(nxt__hkdf_expand_label(suite->hash, st->ks.client_ap, suite->hash_len, "iv",
                                     NULL, 0, iv, 12) == NXT_TLS_OK && in_xcheck(iv, 12),
              "%s: client application iv", name);
        CHECK(nxt__hkdf_expand_label(suite->hash, st->ks.server_ap, suite->hash_len, "key",
                                     NULL, 0, k, suite->key_len) == NXT_TLS_OK &&
              in_xcheck(k, suite->key_len), "%s: server application key", name);
        CHECK(nxt__hkdf_expand_label(suite->hash, st->ks.server_ap, suite->hash_len, "iv",
                                     NULL, 0, iv, 12) == NXT_TLS_OK && in_xcheck(iv, 12),
              "%s: server application iv", name);
    }

    /* Server: NewSessionTicket, then application data, then close_notify. */
    const uint8_t *r;
    size_t rl;
    if ((r = tfield(s, "Record_NewSessionTicket", &rl)) != NULL) {
        buf nst = {0};
        CHECK(open_records(&srv, r, rl, NXT_TLS_CT_HANDSHAKE, &nst, name) == 1 &&
              nst.len >= 4 && nst.p[0] == 4, "%s: NewSessionTicket", name);
        free(nst.p);
    }
    if ((r = tfield(s, "Record_Server_AppData", &rl)) != NULL) {
        size_t want_len;
        const uint8_t *want = need(s, "Server_AppData", &want_len);
        buf app = {0};
        open_records(&srv, r, rl, NXT_TLS_CT_APPLICATION_DATA, &app, name);
        CHECK(app.len == want_len && memcmp(app.p, want, want_len) == 0,
              "%s: server application data", name);
        free(app.p);
        if (xcheck) CHECK(in_xcheck(r + 5, rl - 5), "%s: server app record not in RFC 8448", name);
    }
    static const uint8_t CLOSE_NOTIFY[2] = { 1, 0 };   /* warning, close_notify */
    if ((r = tfield(s, "Record_Server_CloseNotify", &rl)) != NULL) {
        buf al = {0};
        open_records(&srv, r, rl, NXT_TLS_CT_ALERT, &al, name);
        CHECK(al.len == 2 && memcmp(al.p, CLOSE_NOTIFY, 2) == 0, "%s: server close_notify", name);
        free(al.p);
    }

    /* Client: application data, then close_notify. */
    if ((r = tfield(s, "Record_Client_AppData", &rl)) != NULL) {
        size_t dl;
        const uint8_t *d = need(s, "Client_AppData", &dl);
        seal_and_compare(&cli, NXT_TLS_CT_APPLICATION_DATA, d, dl, r, rl, name);
        if (xcheck) CHECK(in_xcheck(r + 5, rl - 5), "%s: client app record not in RFC 8448", name);
    }
    if ((r = tfield(s, "Record_Client_CloseNotify", &rl)) != NULL)
        seal_and_compare(&cli, NXT_TLS_CT_ALERT, CLOSE_NOTIFY, 2, r, rl, name);

    nxt__rprot_wipe(&srv);
    nxt__rprot_wipe(&cli);
    free(tr.p);
    free(plain.p);
    free(expect.p);
    printf("  %s: replayed (%d protected server handshake record%s)\n", name, nrec,
           nrec == 1 ? "" : "s");
}

/* ------------------------------------------------------------------ */
/* Record layer failure paths and the other suites                      */
/* ------------------------------------------------------------------ */

static uint8_t g_rec[NXT_TLS_MAX_RECORD + 64];

static void pair(const nxt__suite *s, const uint8_t *secret, nxt__rprot *tx, nxt__rprot *rx) {
    memset(tx, 0, sizeof(*tx));
    memset(rx, 0, sizeof(*rx));
    CHECK(nxt__rprot_init(tx, s, secret) == NXT_TLS_OK, "pair: tx init");
    CHECK(nxt__rprot_init(rx, s, secret) == NXT_TLS_OK, "pair: rx init");
}

static int all_zero(const uint8_t *p, size_t n) {
    for (size_t i = 0; i < n; i++) if (p[i]) return 0;
    return 1;
}

/* Seal a raw TLSInnerPlaintext (no type byte added) with the key and IV
 * of `secret` at sequence number seq, to build records the sealer refuses
 * to make and to check the nonce independently. Uses nxcrypto directly. */
static size_t raw_seal_seq(const nxt__suite *s, const uint8_t *secret, uint64_t seq,
                           const uint8_t *inner, size_t inner_len, uint8_t *out) {
    uint8_t key[32], iv[12];
    nxt__hkdf_expand_label(s->hash, secret, s->hash_len, "key", NULL, 0, key, s->key_len);
    nxt__hkdf_expand_label(s->hash, secret, s->hash_len, "iv", NULL, 0, iv, 12);
    /* RFC 8446 5.3: the 64-bit sequence number, big-endian, XORed into
     * the last 8 bytes of the IV. */
    for (int i = 0; i < 8; i++) iv[4 + i] ^= (uint8_t)(seq >> (56 - 8 * i));
    size_t body = inner_len + 16;
    out[0] = 23; out[1] = 3; out[2] = 3; out[3] = (uint8_t)(body >> 8); out[4] = (uint8_t)body;
    memcpy(out + 5, inner, inner_len);
    int rc;
    if (s->id == NXT_TLS_CHACHA20_POLY1305_SHA256) {
        rc = nxc_chacha20poly1305_seal(out + 5, out + 5 + inner_len, key, iv, out, 5,
                                       out + 5, inner_len);
    } else {
        nxc_gcm_ctx g;
        nxc_gcm_init(&g, key, s->key_len);
        rc = nxc_gcm_seal(&g, out + 5, out + 5 + inner_len, iv, out, 5, out + 5, inner_len);
        nxc_gcm_wipe(&g);
    }
    CHECK(rc == NXC_OK, "raw_seal");
    return 5 + body;
}

static size_t raw_seal(const nxt__suite *s, const uint8_t *secret, const uint8_t *inner,
                       size_t inner_len, uint8_t *out) {
    return raw_seal_seq(s, secret, 0, inner, inner_len, out);
}

static void test_suite_records(uint16_t id, const char *label) {
    const nxt__suite *s = nxt__suite_find(id);
    CHECK(s != NULL, "%s: suite missing", label);
    if (!s) return;
    uint8_t secret[NXT__MAX_HASH];
    for (size_t i = 0; i < sizeof(secret); i++) secret[i] = (uint8_t)(0xa0 + i);
    nxt__rprot tx, rx;
    uint8_t msg[300];
    for (size_t i = 0; i < sizeof(msg); i++) msg[i] = (uint8_t)(i * 7);
    size_t len = 0, plen = 0;
    uint8_t type = 0;

    /* Round trip, with and without padding, several records in a row. */
    pair(s, secret, &tx, &rx);
    for (size_t pad = 0; pad < 40; pad += 13) {
        CHECK(nxt__rprot_seal(&tx, 23, msg, sizeof(msg), pad, g_rec, sizeof(g_rec), &len)
              == NXT_TLS_OK && len == 5 + sizeof(msg) + 1 + pad + 16, "%s: seal pad %zu", label, pad);
        CHECK(nxt__rprot_open(&rx, g_rec, len, &type, &plen) == NXT_TLS_OK && type == 23 &&
              plen == sizeof(msg) && memcmp(g_rec + 5, msg, plen) == 0,
              "%s: open pad %zu", label, pad);
    }
    /* A record of padding only is fine as long as it has a type. */
    CHECK(nxt__rprot_seal(&tx, 23, NULL, 0, 10, g_rec, sizeof(g_rec), &len) == NXT_TLS_OK &&
          nxt__rprot_open(&rx, g_rec, len, &type, &plen) == NXT_TLS_OK && plen == 0 &&
          type == 23, "%s: empty record", label);

    /* Every single-bit flip in header (AAD), ciphertext or tag is refused
     * and the payload is wiped. Each try uses a fresh receiver at the
     * same sequence number. */
    pair(s, secret, &tx, &rx);
    nxt__rprot_seal(&tx, 22, msg, 40, 0, g_rec, sizeof(g_rec), &len);
    uint8_t orig[128];
    memcpy(orig, g_rec, len);
    int flips_ok = 1;
    for (size_t bit = 0; bit < len * 8; bit++) {
        if (bit / 8 == 3 || bit / 8 == 4) continue;   /* length bytes: caller frames records */
        memcpy(g_rec, orig, len);
        g_rec[bit / 8] ^= (uint8_t)(1u << (bit % 8));
        nxt__rprot r2;
        memset(&r2, 0, sizeof(r2));
        nxt__rprot_init(&r2, s, secret);
        int rc = nxt__rprot_open(&r2, g_rec, len, &type, &plen);
        int want = bit / 8 == 0 ? NXT_TLS_ERR_UNEXPECTED_MESSAGE : NXT_TLS_ERR_BAD_RECORD_MAC;
        if (rc != want || (bit / 8 > 0 && !all_zero(g_rec + 5, len - 5)) || r2.seq != 0)
            flips_ok = 0;
        nxt__rprot_wipe(&r2);
    }
    CHECK(flips_ok, "%s: a tampered record was not refused (or not wiped)", label);

    /* Replay and reordering: a record only opens at its own sequence number. */
    pair(s, secret, &tx, &rx);
    nxt__rprot_seal(&tx, 23, msg, 10, 0, g_rec, sizeof(g_rec), &len);
    memcpy(orig, g_rec, len);
    CHECK(nxt__rprot_open(&rx, g_rec, len, &type, &plen) == NXT_TLS_OK, "%s: first open", label);
    memcpy(g_rec, orig, len);
    CHECK(nxt__rprot_open(&rx, g_rec, len, &type, &plen) == NXT_TLS_ERR_BAD_RECORD_MAC,
          "%s: replayed record accepted", label);

    /* Length limits. */
    pair(s, secret, &tx, &rx);
    uint8_t *big = calloc(NXT_TLS_MAX_PLAINTEXT + 2, 1);
    CHECK(nxt__rprot_seal(&tx, 23, big, NXT_TLS_MAX_PLAINTEXT, 0, g_rec, sizeof(g_rec), &len)
          == NXT_TLS_OK && nxt__rprot_open(&rx, g_rec, len, &type, &plen) == NXT_TLS_OK &&
          plen == NXT_TLS_MAX_PLAINTEXT, "%s: 2^14 byte record", label);
    CHECK(nxt__rprot_seal(&tx, 23, big, NXT_TLS_MAX_PLAINTEXT, 1, g_rec, sizeof(g_rec), &len)
          == NXT_TLS_ERR_RECORD_OVERFLOW, "%s: 2^14 + padding accepted", label);
    CHECK(nxt__rprot_seal(&tx, 23, big, NXT_TLS_MAX_PLAINTEXT + 1, 0, g_rec, sizeof(g_rec), &len)
          == NXT_TLS_ERR_RECORD_OVERFLOW, "%s: 2^14 + 1 bytes accepted", label);
    CHECK(nxt__rprot_seal(&tx, 23, msg, 100, 0, g_rec, 5 + 100 + 1 + 15, &len)
          == NXT_TLS_ERR_BUFFER, "%s: small output buffer", label);
    CHECK(nxt__rprot_seal(&tx, 0, msg, 1, 0, g_rec, sizeof(g_rec), &len) == NXT_TLS_ERR_ARG,
          "%s: content type 0 accepted", label);
    /* Inner plaintext of 2^14 + 2 bytes (content 2^14 + 1): ciphertext is
     * within 2^14 + 256 but the content is too long. */
    big[NXT_TLS_MAX_PLAINTEXT + 1] = 23;
    {
        uint8_t sec2[NXT__MAX_HASH];
        memcpy(sec2, secret, sizeof(sec2));
        size_t rl = raw_seal(s, sec2, big, NXT_TLS_MAX_PLAINTEXT + 2, g_rec);
        nxt__rprot r2;
        memset(&r2, 0, sizeof(r2));
        nxt__rprot_init(&r2, s, sec2);
        CHECK(nxt__rprot_open(&r2, g_rec, rl, &type, &plen) == NXT_TLS_ERR_RECORD_OVERFLOW,
              "%s: overlong inner plaintext accepted", label);
        nxt__rprot_wipe(&r2);
        /* All-zero inner plaintext: no content type. */
        uint8_t zeros[20] = {0};
        rl = raw_seal(s, sec2, zeros, sizeof(zeros), g_rec);
        memset(&r2, 0, sizeof(r2));
        nxt__rprot_init(&r2, s, sec2);
        CHECK(nxt__rprot_open(&r2, g_rec, rl, &type, &plen) == NXT_TLS_ERR_UNEXPECTED_MESSAGE,
              "%s: padding-only record accepted", label);
        nxt__rprot_wipe(&r2);
    }
    free(big);
    /* Ciphertext longer than 2^14 + 256, and too short to hold a tag. */
    memset(g_rec, 0, sizeof(g_rec));
    g_rec[0] = 23; g_rec[1] = 3; g_rec[2] = 3;
    size_t over = NXT_TLS_MAX_CIPHERTEXT + 1;
    g_rec[3] = (uint8_t)(over >> 8); g_rec[4] = (uint8_t)over;
    CHECK(nxt__rprot_open(&rx, g_rec, 5 + over, &type, &plen) == NXT_TLS_ERR_RECORD_OVERFLOW,
          "%s: oversized ciphertext accepted", label);
    g_rec[3] = 0; g_rec[4] = 16;
    CHECK(nxt__rprot_open(&rx, g_rec, 5 + 16, &type, &plen) == NXT_TLS_ERR_BAD_RECORD_MAC,
          "%s: record without content accepted", label);
    g_rec[4] = 20;
    CHECK(nxt__rprot_open(&rx, g_rec, 5 + 19, &type, &plen) == NXT_TLS_ERR_ARG,
          "%s: header length mismatch accepted", label);

    /* The per-record nonce at sequence numbers that use every byte. */
    {
        static const uint64_t seqs[] = { 1, 0xff, 0x100, 0x0102030405060708ull,
                                         0xfedcba9876543210ull, UINT64_MAX - 1 };
        uint8_t inner[11] = { 'n', 'o', 'n', 'c', 'e', ' ', 't', 'e', 's', 't', 23 };
        int ok = 1;
        for (size_t k = 0; k < sizeof(seqs) / sizeof(seqs[0]); k++) {
            size_t rl = raw_seal_seq(s, secret, seqs[k], inner, sizeof(inner), g_rec);
            nxt__rprot r2;
            memset(&r2, 0, sizeof(r2));
            nxt__rprot_init(&r2, s, secret);
            r2.seq = seqs[k];
            if (nxt__rprot_open(&r2, g_rec, rl, &type, &plen) != NXT_TLS_OK || plen != 10 ||
                type != 23 || memcmp(g_rec + 5, "nonce test", 10) != 0 || r2.seq != seqs[k] + 1)
                ok = 0;
            nxt__rprot_wipe(&r2);
        }
        CHECK(ok, "%s: nonce at large sequence numbers", label);
    }

    /* Sequence numbers never wrap. */
    tx.seq = UINT64_MAX - 1;
    CHECK(nxt__rprot_seal(&tx, 23, msg, 1, 0, g_rec, sizeof(g_rec), &len) == NXT_TLS_OK,
          "%s: last sequence number", label);
    CHECK(nxt__rprot_seal(&tx, 23, msg, 1, 0, g_rec, sizeof(g_rec), &len)
          == NXT_TLS_ERR_SEQ_EXHAUSTED, "%s: sequence number wrapped", label);
    rx.seq = UINT64_MAX;
    CHECK(nxt__rprot_open(&rx, g_rec, len, &type, &plen) == NXT_TLS_ERR_SEQ_EXHAUSTED,
          "%s: receive sequence number wrapped", label);

    /* KeyUpdate: the next secret gives different keys, and both sides
     * that update stay in step. */
    uint8_t next[NXT__MAX_HASH];
    memcpy(next, secret, sizeof(next));
    CHECK(nxt__next_traffic_secret(s, next) == NXT_TLS_OK &&
          memcmp(next, secret, s->hash_len) != 0, "%s: traffic upd", label);
    pair(s, next, &tx, &rx);
    nxt__rprot old;
    memset(&old, 0, sizeof(old));
    nxt__rprot_init(&old, s, secret);
    nxt__rprot_seal(&tx, 23, msg, 10, 0, g_rec, sizeof(g_rec), &len);
    memcpy(orig, g_rec, len);
    CHECK(nxt__rprot_open(&old, g_rec, len, &type, &plen) == NXT_TLS_ERR_BAD_RECORD_MAC,
          "%s: old key opened a record after KeyUpdate", label);
    memcpy(g_rec, orig, len);
    CHECK(nxt__rprot_open(&rx, g_rec, len, &type, &plen) == NXT_TLS_OK, "%s: updated key", label);

    /* Wiping leaves nothing usable. */
    nxt__rprot_wipe(&tx);
    CHECK(nxt__rprot_seal(&tx, 23, msg, 1, 0, g_rec, sizeof(g_rec), &len) == NXT_TLS_ERR_ARG &&
          all_zero((const uint8_t *)&tx, sizeof(tx)), "%s: wiped state still usable", label);
    nxt__rprot_wipe(&rx);
    nxt__rprot_wipe(&old);
}

static void test_args(void) {
    const nxt__suite *s = nxt__suite_find(NXT_TLS_AES_128_GCM_SHA256);
    uint8_t sec[32] = {1}, out[64];
    char longl[260];
    memset(longl, 'a', sizeof(longl));
    longl[249] = 0;
    CHECK(nxt__hkdf_expand_label(s->hash, sec, 32, longl, NULL, 0, out, 16) == NXT_TLS_OK,
          "label of 249 bytes refused");
    longl[249] = 'a';
    longl[250] = 0;
    CHECK(nxt__hkdf_expand_label(s->hash, sec, 32, longl, NULL, 0, out, 16) == NXT_TLS_ERR_ARG,
          "label of 250 bytes accepted");
    uint8_t ctx[256] = {0};
    CHECK(nxt__hkdf_expand_label(s->hash, sec, 32, "x", ctx, 256, out, 16) == NXT_TLS_ERR_ARG,
          "context of 256 bytes accepted");
    CHECK(nxt__hkdf_expand_label(s->hash, sec, 31, "x", NULL, 0, out, 16) == NXT_TLS_ERR_ARG,
          "short secret accepted");
    CHECK(nxt__hkdf_expand_label(s->hash, sec, 32, "x", NULL, 0, out, 0) == NXT_TLS_ERR_ARG,
          "empty output accepted");
    CHECK(nxt__suite_find(0x1304) == NULL && nxt__suite_find(0xc02f) == NULL,
          "unsupported suite found");

    nxt__keysched ks;
    memset(&ks, 0, sizeof(ks));
    uint8_t th[48] = {0};
    CHECK(nxt__ks_handshake(&ks, sec, 32, th) == NXT_TLS_ERR_ARG, "handshake before init");
    CHECK(nxt__ks_init(&ks, s) == NXT_TLS_OK, "ks_init");
    CHECK(nxt__ks_application(&ks, th) == NXT_TLS_ERR_ARG, "application before handshake");
    CHECK(nxt__ks_handshake(&ks, sec, 32, th) == NXT_TLS_OK, "ks_handshake");
    CHECK(nxt__ks_handshake(&ks, sec, 32, th) == NXT_TLS_ERR_ARG, "handshake twice");
    CHECK(nxt__ks_application(&ks, th) == NXT_TLS_OK && all_zero(ks.client_hs, 48) &&
          all_zero(ks.server_hs, 48), "handshake secrets kept after application stage");
    nxt__ks_wipe(&ks);
    CHECK(all_zero((const uint8_t *)&ks, sizeof(ks)), "ks_wipe");
}

int main(int argc, char **argv) {
    if (argc != 2) {
        printf("usage: %s <vector dir>\n", argv[0]);
        return 2;
    }
    char *trace = vec_read_file(argv[1], "rfc8448/transcripts.vec");
    vec_parse(trace, trace_cb, NULL);
    g_xcheck = vec_read_file(argv[1], "rfc8448/mbedtls_ssl.data");
    for (char *c = g_xcheck; *c; c++) *c = (char)tolower((unsigned char)*c);

    replay_state st;
    memset(&st, 0, sizeof(st));
    /* RFC 8448 section 3 (cross-checked against the second transcription)
     * and section 7 (middlebox compatibility mode, change_cipher_spec records). */
    replay("Simple_1RTT_Handshake", 1, 0, &st);
    nxt__ks_wipe(&st.ks);
    replay("Middlebox_Compatibility_Mode", 0, 0, &st);
    nxt__ks_wipe(&st.ks);
    /* Section 6: the server asks for a client certificate and signs with ECDSA. */
    replay("Client_Authentication_Handshake", 0, 1, &st);
    nxt__ks_wipe(&st.ks);

    test_suite_records(NXT_TLS_AES_128_GCM_SHA256, "AES-128-GCM");
    test_suite_records(NXT_TLS_AES_256_GCM_SHA384, "AES-256-GCM");
    test_suite_records(NXT_TLS_CHACHA20_POLY1305_SHA256, "ChaCha20-Poly1305");
    test_args();

    for (int i = 0; i < g_nsect; i++)
        for (int j = 0; j < g_sect[i].n; j++) { free(g_sect[i].key[j]); free(g_sect[i].val[j]); }
    free(trace);
    free(g_xcheck);
    return vec_report("crypto_tls13_trace_test");
}
