/*
 * Host test for the nxtls TLS 1.3 client (lib/nxtls/src/tls_client.c).
 *
 * Part 1 replays RFC 8448 through the real client API: the client is
 * handed the trace's ClientHello and X25519 key (test hook), the
 * server's records are fed to it through a scripted transport, and every
 * byte the client sends must equal the trace: its Finished, its
 * application data and its close_notify. Run once with whole records and
 * once one byte at a time.
 *
 * Part 2 runs the client against a scripted TLS 1.3 server written here
 * on top of the nxtls key schedule, with Ed25519 certificates made by
 * crypto_certgen.h. A normal handshake must work (with an intermediate,
 * a HelloRetryRequest cookie, a CertificateRequest, ALPN, KeyUpdate,
 * tickets, fragmentation, close_notify), and each broken rule must end
 * the handshake with the right error and the right alert on the wire:
 * certificate refusals (expired, self-signed, wrong host, untrusted
 * root, unreadable), bad signatures and Finished, downgrade to TLS 1.2,
 * illegal or unsolicited extensions, wrong message order, tampered
 * records and more.
 *
 * Usage: crypto_tls13_client_test <vector dir>   (built with -DNXT_TESTING)
 *
 * Host-only (uses the C library). Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include "crypto_vec.h"
#include "crypto_certgen.h"
#include "nxtls/tls.h"
#include "nxcrypto.h"
#include "../../lib/nxtls/src/tls13.h"

#ifndef NXT_TESTING
#error "build this test with -DNXT_TESTING"
#endif

typedef struct { uint8_t *p; size_t len, cap; } buf;

static void buf_add(buf *b, const void *p, size_t n) {
    if (b->len + n > b->cap) {
        b->cap = (b->len + n) * 2 + 256;
        b->p = realloc(b->p, b->cap);
        if (!b->p) { perror("realloc"); exit(2); }
    }
    if (n) memcpy(b->p + b->len, p, n);
    b->len += n;
}

static void buf_free(buf *b) { free(b->p); b->p = NULL; b->len = b->cap = 0; }

static nxt_tls_conn g_conn;

/* ------------------------------------------------------------------ */
/* Scripted transport                                                   */
/* ------------------------------------------------------------------ */

typedef struct server server;

typedef struct {
    buf in;              /* bytes the client will receive */
    size_t in_pos;
    size_t chunk;        /* most bytes per recv (1 = byte at a time) */
    buf out;             /* bytes the client sent */
    server *srv;         /* if set, produces the next flight when in runs dry */
    int recv_calls;
} mock;

static void srv_step(server *s);

static long m_send(void *ctx, const uint8_t *d, size_t n) {
    mock *m = ctx;
    buf_add(&m->out, d, n);
    return (long)n;
}

static int srv_wants_step(const server *s);

static long m_recv(void *ctx, uint8_t *b, size_t cap) {
    mock *m = ctx;
    m->recv_calls++;
    if (m->in_pos == m->in.len && m->srv && srv_wants_step(m->srv)) srv_step(m->srv);
    if (m->in_pos == m->in.len) return 0;
    size_t k = m->in.len - m->in_pos;
    if (k > cap) k = cap;
    if (m->chunk && k > m->chunk) k = m->chunk;
    memcpy(b, m->in.p + m->in_pos, k);
    m->in_pos += k;
    return (long)k;
}

/* Deterministic "random" for the client: a counter stream. */
static int fake_random(void *ctx, uint8_t *out, size_t len) {
    unsigned *ctr = ctx;
    for (size_t i = 0; i < len; i++) out[i] = (uint8_t)(0x5a ^ (*ctr)++ * 31);
    return 0;
}

static int failing_random(void *ctx, uint8_t *out, size_t len) {
    (void)ctx; (void)out; (void)len;
    return -1;
}

/* ------------------------------------------------------------------ */
/* Part 1: RFC 8448 replay through the client API                       */
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

static void trace_cb(void *ctx, const char *name, const char *value) {
    (void)ctx;
    if (name[0] == 0) return;
    if (!strcmp(name, "[")) {
        if (g_nsect == MAX_SECTIONS) { printf("ERROR: too many sections\n"); exit(2); }
        snprintf(g_sect[g_nsect++].name, 64, "%s", value);
        return;
    }
    if (g_nsect == 0) return;
    section *s = &g_sect[g_nsect - 1];
    char *clean = vec_strdup(value);
    size_t k = 0;
    for (size_t i = 0; clean[i]; i++) if (!isspace((unsigned char)clean[i])) clean[k++] = clean[i];
    clean[k] = 0;
    size_t len = 0;
    uint8_t *v = hex_decode(clean, &len);
    free(clean);
    if (!v) return;                     /* not hex (timestamps, identities): unused */
    if (s->n == MAX_FIELDS) { printf("ERROR: too many fields\n"); exit(2); }
    s->key[s->n] = vec_strdup(name);
    s->val[s->n] = v;
    s->len[s->n] = len;
    s->n++;
}

static const section *find_section(const char *name) {
    for (int i = 0; i < g_nsect; i++)
        if (!strcmp(g_sect[i].name, name)) return &g_sect[i];
    printf("ERROR: no trace section %s\n", name);
    exit(2);
}

static const uint8_t *tf(const section *s, const char *name, size_t *len) {
    for (int i = 0; i < s->n; i++)
        if (!strcmp(s->key[i], name)) { *len = s->len[i]; return s->val[i]; }
    *len = 0;
    return NULL;
}

static const uint8_t *tneed(const section *s, const char *name, size_t *len) {
    const uint8_t *v = tf(s, name, len);
    if (!v) { printf("ERROR: %s has no %s\n", s->name, name); exit(2); }
    return v;
}

/* Find needle in hay; returns offset or -1. */
static long find_bytes(const uint8_t *hay, size_t n, const uint8_t *needle, size_t k) {
    for (size_t i = 0; i + k <= n; i++)
        if (!memcmp(hay + i, needle, k)) return (long)i;
    return -1;
}

/* The trace's server key: RSA-1024 modulus or P-256 point, located by its
 * DER prefix inside the Certificate message. */
static int trace_server_key(const section *s) {
    size_t n;
    const uint8_t *cert = tneed(s, "Message_Server_Certificate", &n);
    static const uint8_t rsa_hdr[] = { 0x30, 0x81, 0x89, 0x02, 0x81, 0x81, 0x00 };
    static const uint8_t ec_hdr[] = { 0x03, 0x42, 0x00, 0x04 };
    static const uint8_t e65537[] = { 0x01, 0x00, 0x01 };
    long at = find_bytes(cert, n, rsa_hdr, sizeof(rsa_hdr));
    if (at >= 0 && (size_t)at + sizeof(rsa_hdr) + 128 <= n)
        return nxt__tls_test_server_key(&g_conn, NXT_KEY_RSA, cert + at + sizeof(rsa_hdr), 128,
                                        e65537, 3);
    at = find_bytes(cert, n, ec_hdr, sizeof(ec_hdr));
    if (at >= 0 && (size_t)at + 3 + 65 <= n)
        return nxt__tls_test_server_key(&g_conn, NXT_KEY_EC_P256, cert + at + 3, 65, NULL, 0);
    return -1;
}

static void replay_trace(const char *name, size_t chunk, int cert_request) {
    const section *s = find_section(name);
    size_t n, chr_len, pool_len;
    const uint8_t *chr = tneed(s, "Record_ClientHello_1", &chr_len);
    const uint8_t *pool = tneed(s, "Client_RNG_Pool", &pool_len);
    size_t sid_len = chr[5 + 4 + 2 + 32];
    CHECK(pool_len >= 64 + sid_len, "%s: pool", name);

    mock m = { .chunk = chunk };
    const uint8_t *r = tneed(s, "Record_ServerHello", &n);
    buf_add(&m.in, r, n);
    r = tneed(s, "Record_ServerHandshakeMessages", &n);
    buf_add(&m.in, r, n);
    static const char *const later[] = { "Record_NewSessionTicket", "Record_Server_AppData",
                                         "Record_Server_CloseNotify" };
    for (size_t i = 0; i < 3; i++)
        if ((r = tf(s, later[i], &n)) != NULL) buf_add(&m.in, r, n);

    unsigned ctr = 0;
    nxt_trust_store none = { NULL, 0 };
    nxt_tls_config cfg = { "server", &none, 0, { &m, m_send, m_recv }, fake_random, &ctr, NULL };
    CHECK(nxt__tls_test_hello(&g_conn, chr + 5, chr_len - 5, pool + 32 + sid_len) == NXT_TLS_OK,
          "%s: test hello", name);
    CHECK(trace_server_key(s) == NXT_TLS_OK, "%s: server key", name);
    int rc = nxt_tls_connect(&g_conn, &cfg);
    CHECK(rc == NXT_TLS_OK, "%s (chunk %zu): connect: %s (alert sent %d)", name, chunk,
          nxt_tls_err_str(rc), nxt_tls_get_info(&g_conn)->alert_sent);
    if (rc != NXT_TLS_OK) { buf_free(&m.in); buf_free(&m.out); return; }
    CHECK(ctr == 0, "%s: random source used despite the test hello", name);
    CHECK(nxt_tls_get_info(&g_conn)->suite == NXT_TLS_AES_128_GCM_SHA256, "%s: suite", name);
    CHECK(nxt_tls_get_info(&g_conn)->cert_requested == cert_request, "%s: cert request", name);

    /* What the client sent: exactly the trace's ClientHello record, then
     * (unless it had to answer a CertificateRequest) its Finished. */
    CHECK(m.out.len >= chr_len && !memcmp(m.out.p, chr, chr_len), "%s: ClientHello record", name);
    size_t cf_len;
    const uint8_t *cf = tneed(s, "Record_ClientFinished", &cf_len);
    if (!cert_request)
        CHECK(m.out.len == chr_len + cf_len && !memcmp(m.out.p + chr_len, cf, cf_len),
              "%s (chunk %zu): client Finished differs from RFC 8448", name, chunk);
    size_t mark = m.out.len;

    if ((r = tf(s, "Client_AppData", &n)) != NULL) {
        size_t want_len;
        const uint8_t *want = tneed(s, "Record_Client_AppData", &want_len);
        CHECK(nxt_tls_write(&g_conn, r, n) == (long)n, "%s: write", name);
        CHECK(m.out.len - mark == want_len && !memcmp(m.out.p + mark, want, want_len),
              "%s: client application data record differs from RFC 8448", name);
        mark = m.out.len;
    }
    if ((r = tf(s, "Server_AppData", &n)) != NULL) {
        uint8_t got[256];
        size_t total = 0;
        long k;
        while (total < n && (k = nxt_tls_read(&g_conn, got + total, sizeof(got) - total)) > 0)
            total += (size_t)k;
        CHECK(total == n && !memcmp(got, r, n), "%s: server application data", name);
    }
    if (tf(s, "Record_Server_CloseNotify", &n) != NULL && !cert_request) {
        uint8_t b[16];
        CHECK(nxt_tls_read(&g_conn, b, sizeof(b)) == 0, "%s: close_notify read as end", name);
        CHECK(nxt_tls_get_info(&g_conn)->alert_received == 0, "%s: close_notify noted", name);
    }
    if ((r = tf(s, "Record_Client_CloseNotify", &n)) != NULL && !cert_request) {
        CHECK(nxt_tls_close(&g_conn) == NXT_TLS_OK, "%s: close", name);
        CHECK(m.out.len - mark == n && !memcmp(m.out.p + mark, r, n),
              "%s: client close_notify record differs from RFC 8448", name);
    } else {
        nxt_tls_close(&g_conn);
    }
    printf("  RFC 8448 %s (%s): client output matches\n", name,
           chunk == 1 ? "1 byte per read" : "whole records");
    buf_free(&m.in);
    buf_free(&m.out);
}

/* The trace with its RSA-1024 self-signed certificate and no test key:
 * the real certificate check must refuse it. */
static void replay_trace_real_cert(void) {
    const section *s = find_section("Simple_1RTT_Handshake");
    size_t n, chr_len, pool_len;
    const uint8_t *chr = tneed(s, "Record_ClientHello_1", &chr_len);
    const uint8_t *pool = tneed(s, "Client_RNG_Pool", &pool_len);
    mock m = { .chunk = 0 };
    const uint8_t *r = tneed(s, "Record_ServerHello", &n);
    buf_add(&m.in, r, n);
    r = tneed(s, "Record_ServerHandshakeMessages", &n);
    buf_add(&m.in, r, n);
    unsigned ctr = 0;
    nxt_trust_store none = { NULL, 0 };
    nxt_tls_config cfg = { "server", &none, 1500000000, { &m, m_send, m_recv }, fake_random, &ctr,
                           NULL };
    memset(&g_conn, 0, sizeof(g_conn));
    nxt__tls_test_hello(&g_conn, chr + 5, chr_len - 5, pool + 32);
    int rc = nxt_tls_connect(&g_conn, &cfg);
    const nxt_tls_info *in = nxt_tls_get_info(&g_conn);
    CHECK(rc == NXT_TLS_ERR_CERTIFICATE && in->cert_error == NXT_X509_ERR_WEAK_KEY &&
          in->cert_failed_index == 0 && in->alert_sent == NXT_TLS_ALERT_BAD_CERTIFICATE,
          "RFC 8448 certificate (RSA-1024) not refused: %s / %s / alert %d", nxt_tls_err_str(rc),
          nxt_x509_err_str(in->cert_error), in->alert_sent);
    buf_free(&m.in);
    buf_free(&m.out);
}

/* ------------------------------------------------------------------ */
/* Part 2: a scripted TLS 1.3 server                                    */
/* ------------------------------------------------------------------ */

enum {
    M_NONE,
    /* ServerHello */
    M_SH_TLS12, M_SH_BAD_SID, M_SH_SUITE_NOT_OFFERED, M_SH_LOW_ORDER, M_SH_GROUP_P256,
    M_SH_NO_KEY_SHARE, M_SH_DUP_EXT, M_SH_ALPN, M_SH_TRUNCATED, M_SH_COMPRESSION,
    M_SH_VERSION_13_LEGACY, M_PLAIN_ALERT, M_EOF_AFTER_SH, M_SH_AND_EE_ONE_RECORD,
    M_CCS_BAD, M_PLAIN_APPDATA, M_RECORD_OVERFLOW, M_SH_SV_TLS12,
    /* HelloRetryRequest */
    M_HRR_COOKIE, M_HRR_KEY_SHARE, M_HRR_TWICE, M_HRR_EMPTY, M_HRR_SUITE_CHANGE,
    /* encrypted flight */
    M_EE_UNSOLICITED, M_EE_KEY_SHARE, M_EE_WRONG_ALPN, M_EE_RECORD_LIMIT, M_EE_RECORD_LIMIT_63,
    M_NO_EE,
    M_CR, M_CR_NO_SIGALGS, M_CERT_EMPTY, M_CERT_CONTEXT, M_CERT_ENTRY_EXT,
    M_CV_BAD_SIG, M_CV_PKCS1, M_CV_WRONG_TYPE, M_NO_CV, M_FIN_BAD, M_FIN_SHORT, M_FIN_LONG,
    M_APP_BEFORE_FIN, M_BAD_RECORD_MAC, M_CCS_IN_FLIGHT, M_FLIGHT_SPLIT, M_EMPTY_HS_RECORD,
    M_UNKNOWN_HS_MESSAGE, M_TOO_LARGE,
};

#define MAX_CHAIN 20

struct server {
    int mut;
    mock *m;
    uint16_t suite;
    const char *alpn;                   /* selected ALPN, or NULL */
    uint8_t *chain[MAX_CHAIN];
    size_t chain_len[MAX_CHAIN];
    int nchain;
    uint8_t leaf_seed[32];
    int phase;                          /* 0 wait CH1, 1 wait CH2, 2 flight sent, 3 done */
    int hrr_sent;
    const nxt__suite *s;
    nxc_hash_ctx th;
    nxt__keysched ks;
    nxt__rprot rx, tx;
    uint8_t c_hs[NXT__MAX_HASH], c_ap[NXT__MAX_HASH], s_ap[NXT__MAX_HASH];
    uint8_t th_after_fin[NXT__MAX_HASH];
    uint8_t ch_random[32], sid[32], client_pub[32];
    size_t sid_len;
    int ch_had_cookie, ch_had_sni, ch_had_rsl, ch2_same;
    unsigned ch_rsl;                    /* record_size_limit the client offered */
    int alert_level;                    /* level byte of the last alert seen */
    int stray_records;                  /* client records that fit no expected pattern */
    size_t max_inner;                   /* largest inner plaintext (content + type) seen */
    size_t out_seen;                    /* client output bytes already read */
    int client_finished_ok, client_sent_empty_cert;
    uint8_t ch1_random[32];
    buf ccs_seen;
};

static int srv_wants_step(const server *s) { return s->phase < 2; }

static void th_add(server *s, const uint8_t *p, size_t n) {
    if (nxc_hash_update(&s->th, p, n) != NXC_OK) { printf("ERROR: th\n"); exit(2); }
}

static void th_now(server *s, uint8_t *out) {
    nxc_hash_ctx c = s->th;
    nxc_hash_final(&c, out, NXT__MAX_HASH);
}

static void plain_record(server *s, uint8_t type, const uint8_t *p, size_t n) {
    uint8_t h[5] = { type, 3, 3, (uint8_t)(n >> 8), (uint8_t)n };
    buf_add(&s->m->in, h, 5);
    buf_add(&s->m->in, p, n);
}

static void sealed_record(server *s, uint8_t type, const uint8_t *p, size_t n) {
    static uint8_t rec[NXT_TLS_MAX_RECORD + 64];
    size_t len = 0;
    int rc = nxt__rprot_seal(&s->tx, type, p, n, 0, rec, sizeof(rec), &len);
    if (rc != NXT_TLS_OK) { printf("ERROR: server seal: %s\n", nxt_tls_err_str(rc)); exit(2); }
    buf_add(&s->m->in, rec, len);
}

static void u16(buf *b, unsigned v) { uint8_t x[2] = { (uint8_t)(v >> 8), (uint8_t)v }; buf_add(b, x, 2); }
static void u24(buf *b, size_t v) {
    uint8_t x[3] = { (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v };
    buf_add(b, x, 3);
}
static void u8(buf *b, unsigned v) { uint8_t x = (uint8_t)v; buf_add(b, &x, 1); }

/* Wrap body as a handshake message of type t. */
static void hs_msg(buf *out, uint8_t t, const buf *body) {
    u8(out, t);
    u24(out, body->len);
    buf_add(out, body->p, body->len);
}

static void ext(buf *b, unsigned type, const void *p, size_t n) {
    u16(b, type);
    u16(b, (unsigned)n);
    buf_add(b, p, n);
}

/* Read the next record the client sent (from the server's position). */
static int next_client_record(server *s, uint8_t *type, const uint8_t **body, size_t *len) {
    buf *o = &s->m->out;
    if (s->out_seen + 5 > o->len) return 0;
    size_t n = (size_t)o->p[s->out_seen + 3] << 8 | o->p[s->out_seen + 4];
    if (s->out_seen + 5 + n > o->len) return 0;
    *type = o->p[s->out_seen];
    *body = o->p + s->out_seen + 5;
    *len = n;
    s->out_seen += 5 + n;
    return 1;
}

/* Parse the client's ClientHello: random, session id, X25519 share, cookie. */
static void parse_ch(server *s, const uint8_t *m, size_t n) {
    size_t i = 4 + 2;
    CHECK(n > 4 + 2 + 32 + 1 && m[0] == 1, "server: not a ClientHello");
    memcpy(s->ch_random, m + i, 32);
    i += 32;
    s->sid_len = m[i];
    memcpy(s->sid, m + i + 1, s->sid_len);
    i += 1 + s->sid_len;
    size_t sl = (size_t)m[i] << 8 | m[i + 1];
    int has_suite = 0;
    for (size_t k = 0; k < sl; k += 2)
        if (((unsigned)m[i + 2 + k] << 8 | m[i + 3 + k]) == s->suite) has_suite = 1;
    CHECK(has_suite || s->mut == M_SH_SUITE_NOT_OFFERED, "server: suite %04x not offered", s->suite);
    i += 2 + sl;
    i += 1 + m[i];
    size_t end = i + 2 + ((size_t)m[i] << 8 | m[i + 1]);
    i += 2;
    s->ch_had_cookie = s->ch_had_sni = s->ch_had_rsl = 0;
    while (i + 4 <= end) {
        unsigned t = (unsigned)m[i] << 8 | m[i + 1];
        size_t l = (size_t)m[i + 2] << 8 | m[i + 3];
        if (t == 51) {
            CHECK(l == 2 + 4 + 32 && m[i + 6] == 0 && m[i + 7] == 0x1d, "server: key_share");
            memcpy(s->client_pub, m + i + 4 + 6, 32);
        }
        if (t == 44) s->ch_had_cookie = 1;
        if (t == 0) s->ch_had_sni = 1;
        if (t == 28 && l == 2) { s->ch_had_rsl = 1; s->ch_rsl = (unsigned)m[i + 4] << 8 | m[i + 5]; }
        i += 4 + l;
    }
}

static const uint8_t SERVER_X25519[32] = { 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
                                           7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7 };
static const uint8_t HRR_RAND[32] = {
    0xcf, 0x21, 0xad, 0x74, 0xe5, 0x9a, 0x61, 0x11, 0xbe, 0x1d, 0x8c, 0x02, 0x1e, 0x65, 0xb8, 0x91,
    0xc2, 0xa2, 0x11, 0x16, 0x7a, 0xbb, 0x8c, 0x5e, 0x07, 0x9e, 0x09, 0xe2, 0xc8, 0xa8, 0x33, 0x9c };

static void send_hrr(server *s, int key_share, int cookie, uint16_t suite) {
    buf body = {0}, exts = {0}, msg = {0};
    u16(&body, 0x0303);
    buf_add(&body, HRR_RAND, 32);
    u8(&body, (unsigned)s->sid_len);
    buf_add(&body, s->sid, s->sid_len);
    u16(&body, suite);
    u8(&body, 0);
    uint8_t sv[2] = { 3, 4 };
    ext(&exts, 43, sv, 2);
    if (key_share) { uint8_t g[2] = { 0, 0x17 }; ext(&exts, 51, g, 2); }
    if (cookie) { uint8_t ck[7] = { 0, 5, 'c', 'o', 'o', 'k', 'y' }; ext(&exts, 44, ck, 7); }
    u16(&body, (unsigned)exts.len);
    buf_add(&body, exts.p, exts.len);
    hs_msg(&msg, 2, &body);
    plain_record(s, 22, msg.p, msg.len);
    th_add(s, msg.p, msg.len);
    buf_free(&body); buf_free(&exts); buf_free(&msg);
}

static void sign_cv(server *s, uint8_t *sig) {
    uint8_t th[NXT__MAX_HASH], content[64 + 34 + NXT__MAX_HASH];
    th_now(s, th);
    memset(content, 0x20, 64);
    memcpy(content + 64, "TLS 1.3, server CertificateVerify", 34);
    memcpy(content + 98, th, s->s->hash_len);
    nxc_ed25519_sign(sig, content, 98 + s->s->hash_len, s->leaf_seed);
}

/* Build and queue the server's reply to the client's latest ClientHello. */
static void srv_step(server *s) {
    uint8_t type;
    const uint8_t *body;
    size_t len;
    if (!next_client_record(s, &type, &body, &len)) return;
    CHECK(type == 22, "server: expected a ClientHello record");
    s->s = nxt__suite_find(s->suite == 0x1304 ? NXT_TLS_AES_128_GCM_SHA256 : s->suite);

    if (s->phase == 0) {
        parse_ch(s, body, len);
        memcpy(s->ch1_random, s->ch_random, 32);
        nxc_hash_init(&s->th, s->s->hash);
        th_add(s, body, len);
        if (s->mut == M_PLAIN_ALERT) {
            uint8_t a[2] = { 2, 40 };
            plain_record(s, 21, a, 2);
            s->phase = 3;
            return;
        }
        if (s->mut == M_HRR_COOKIE || s->mut == M_HRR_KEY_SHARE || s->mut == M_HRR_TWICE ||
            s->mut == M_HRR_EMPTY || s->mut == M_HRR_SUITE_CHANGE) {
            uint8_t h[NXT__MAX_HASH], mh[4] = { 254, 0, 0, s->s->hash_len };
            th_now(s, h);
            nxc_hash_init(&s->th, s->s->hash);
            th_add(s, mh, 4);
            th_add(s, h, s->s->hash_len);
            send_hrr(s, s->mut == M_HRR_KEY_SHARE, s->mut != M_HRR_EMPTY, s->suite);
            s->phase = 1;
            return;
        }
    } else {
        parse_ch(s, body, len);
        s->ch2_same = !memcmp(s->ch_random, s->ch1_random, 32);
        th_add(s, body, len);
        if (s->mut == M_HRR_TWICE) {
            send_hrr(s, 0, 1, s->suite);
            s->phase = 3;
            return;
        }
        if (s->mut == M_HRR_SUITE_CHANGE) s->suite = s->suite == 0x1301 ? 0x1303 : 0x1301;
    }

    /* ServerHello */
    buf sh = {0}, exts = {0}, msg = {0};
    u16(&sh, s->mut == M_SH_VERSION_13_LEGACY ? 0x0304 : 0x0303);
    uint8_t rnd[32];
    memset(rnd, 0x42, 32);
    buf_add(&sh, rnd, 32);
    if (s->mut == M_SH_BAD_SID) { u8(&sh, 32); uint8_t z[32] = {0}; buf_add(&sh, z, 32); }
    else { u8(&sh, (unsigned)s->sid_len); buf_add(&sh, s->sid, s->sid_len); }
    u16(&sh, s->mut == M_SH_SUITE_NOT_OFFERED ? 0x1304 : s->suite);
    u8(&sh, s->mut == M_SH_COMPRESSION ? 1 : 0);
    uint8_t pub[32], shared[32];
    nxc_x25519_public(pub, SERVER_X25519);
    if (s->mut != M_SH_TLS12) {
        uint8_t sv[2] = { 3, s->mut == M_SH_SV_TLS12 ? 3 : 4 };
        ext(&exts, 43, sv, 2);
    }
    if (s->mut == M_SH_DUP_EXT) { uint8_t sv[2] = { 3, 4 }; ext(&exts, 43, sv, 2); }
    if (s->mut == M_SH_ALPN) { uint8_t a[5] = { 0, 3, 2, 'h', '2' }; ext(&exts, 16, a, 5); }
    if (s->mut != M_SH_NO_KEY_SHARE) {
        uint8_t ks[36] = { 0, 0x1d, 0, 32 };
        if (s->mut == M_SH_GROUP_P256) ks[1] = 0x17;
        if (s->mut != M_SH_LOW_ORDER) memcpy(ks + 4, pub, 32);   /* all-zero point otherwise */
        ext(&exts, 51, ks, 36);
    }
    u16(&sh, (unsigned)exts.len);
    buf_add(&sh, exts.p, exts.len);
    hs_msg(&msg, 2, &sh);
    if (s->mut == M_SH_TRUNCATED) { msg.len -= 3; msg.p[3] -= 3; }
    th_add(s, msg.p, msg.len);
    if (s->mut == M_RECORD_OVERFLOW) {
        uint8_t h[5] = { 22, 3, 3, 0x40, 0x01 };              /* 2^14 + 1 bytes */
        buf_add(&s->m->in, h, 5);
        uint8_t *junk = calloc(16385, 1);
        buf_add(&s->m->in, junk, 16385);
        free(junk);
        s->phase = 3;
        goto done;
    }
    if (s->mut == M_PLAIN_APPDATA) {
        plain_record(s, 23, (const uint8_t *)"hi", 2);
        s->phase = 3;
        goto done;
    }

    nxc_x25519(shared, SERVER_X25519, s->client_pub);
    uint8_t th[NXT__MAX_HASH];
    th_now(s, th);
    nxt__ks_init(&s->ks, s->s);
    nxt__ks_handshake(&s->ks, shared, 32, th);
    memcpy(s->c_hs, s->ks.client_hs, NXT__MAX_HASH);
    memset(&s->tx, 0, sizeof(s->tx));
    memset(&s->rx, 0, sizeof(s->rx));
    nxt__rprot_init(&s->tx, s->s, s->ks.server_hs);
    nxt__rprot_init(&s->rx, s->s, s->ks.client_hs);

    /* Encrypted flight. */
    buf flight = {0}, b = {0};
    if (s->mut != M_NO_EE) {
        buf e = {0};
        if (s->mut == M_EE_UNSOLICITED) ext(&e, 5, NULL, 0);
        if (s->mut == M_EE_KEY_SHARE) ext(&e, 51, pub, 4);
        if (s->mut == M_EE_RECORD_LIMIT || s->mut == M_EE_RECORD_LIMIT_63) {
            uint8_t l[2] = { 0, s->mut == M_EE_RECORD_LIMIT ? 64 : 63 };
            ext(&e, 28, l, 2);
        }
        if (s->alpn || s->mut == M_EE_WRONG_ALPN) {
            const char *a = s->mut == M_EE_WRONG_ALPN ? "h2" : s->alpn;
            uint8_t v[64];
            size_t al = strlen(a);
            v[0] = 0; v[1] = (uint8_t)(al + 1); v[2] = (uint8_t)al;
            memcpy(v + 3, a, al);
            ext(&e, 16, v, al + 3);
        }
        if (s->ch_had_sni) ext(&e, 0, NULL, 0);                  /* server_name acknowledged */
        uint8_t groups[4] = { 0, 2, 0, 0x1d };
        ext(&e, 10, groups, 4);
        u16(&b, (unsigned)e.len);
        buf_add(&b, e.p, e.len);
        hs_msg(&flight, 8, &b);
        buf_free(&e);
        b.len = 0;
    }
    if (s->mut == M_SH_AND_EE_ONE_RECORD) {
        /* ServerHello and EncryptedExtensions in one plaintext record. */
        buf both = {0};
        buf_add(&both, msg.p, msg.len);
        buf_add(&both, flight.p, flight.len);
        plain_record(s, 22, both.p, both.len);
        buf_free(&both);
        s->phase = 3;
        buf_free(&flight);
        buf_free(&b);
        goto done;
    }
    plain_record(s, 22, msg.p, msg.len);
    if (s->mut == M_EOF_AFTER_SH) { s->phase = 3; buf_free(&flight); buf_free(&b); goto done; }
    if (s->mut == M_CCS_BAD) {
        uint8_t v = 2;
        plain_record(s, 20, &v, 1);
    } else if (s->sid_len) {
        uint8_t v = 1;
        plain_record(s, 20, &v, 1);                              /* compatibility mode */
    }
    th_add(s, flight.p, flight.len);
    if (s->mut == M_CR || s->mut == M_CR_NO_SIGALGS) {
        buf e = {0};
        u8(&b, 3);
        buf_add(&b, "ctx", 3);
        if (s->mut == M_CR) { uint8_t sa[4] = { 0, 2, 8, 7 }; ext(&e, 13, sa, 4); }
        else ext(&e, 47, NULL, 0);                               /* only certificate_authorities */
        u16(&b, (unsigned)e.len);
        buf_add(&b, e.p, e.len);
        size_t at = flight.len;
        hs_msg(&flight, 13, &b);
        th_add(s, flight.p + at, flight.len - at);
        buf_free(&e);
        b.len = 0;
    }
    /* Certificate */
    u8(&b, s->mut == M_CERT_CONTEXT ? 1 : 0);
    if (s->mut == M_CERT_CONTEXT) u8(&b, 9);
    buf list = {0};
    if (s->mut != M_CERT_EMPTY)
        for (int i = 0; i < s->nchain; i++) {
            u24(&list, s->chain_len[i]);
            buf_add(&list, s->chain[i], s->chain_len[i]);
            if (s->mut == M_CERT_ENTRY_EXT && i == 0) { u16(&list, 4); u16(&list, 5); u16(&list, 0); }
            else u16(&list, 0);
        }
    if (s->mut == M_TOO_LARGE) {
        uint8_t *junk = calloc(NXT_TLS_MAX_HANDSHAKE, 1);
        u24(&list, NXT_TLS_MAX_HANDSHAKE);
        buf_add(&list, junk, NXT_TLS_MAX_HANDSHAKE);
        u16(&list, 0);
        free(junk);
    }
    u24(&b, list.len);
    buf_add(&b, list.p, list.len);
    buf_free(&list);
    size_t at = flight.len;
    hs_msg(&flight, 11, &b);
    th_add(s, flight.p + at, flight.len - at);
    b.len = 0;
    /* CertificateVerify */
    if (s->mut != M_NO_CV) {
        uint8_t sig[64];
        sign_cv(s, sig);
        if (s->mut == M_CV_BAD_SIG) sig[10] ^= 1;
        unsigned scheme = s->mut == M_CV_PKCS1 ? 0x0401 : s->mut == M_CV_WRONG_TYPE ? 0x0403 : 0x0807;
        u16(&b, scheme);
        u16(&b, 64);
        buf_add(&b, sig, 64);
        at = flight.len;
        hs_msg(&flight, 15, &b);
        th_add(s, flight.p + at, flight.len - at);
        b.len = 0;
    }
    if (s->mut == M_UNKNOWN_HS_MESSAGE) {
        u8(&b, 0);
        at = flight.len;
        hs_msg(&flight, 99, &b);
        b.len = 0;
    }
    /* Finished */
    uint8_t mac[NXT__MAX_HASH];
    th_now(s, th);
    nxt__finished_mac(s->s, s->ks.server_hs, th, mac);
    if (s->mut == M_FIN_BAD) mac[0] ^= 0x80;
    buf_add(&b, mac, s->mut == M_FIN_SHORT ? s->s->hash_len - 1 : s->s->hash_len);
    if (s->mut == M_FIN_LONG) u8(&b, 0);
    at = flight.len;
    hs_msg(&flight, 20, &b);
    th_add(s, flight.p + at, flight.len - at);
    th_now(s, s->th_after_fin);
    buf_free(&b);

    if (s->mut == M_APP_BEFORE_FIN) {
        sealed_record(s, 23, (const uint8_t *)"early", 5);
    } else if (s->mut == M_FLIGHT_SPLIT) {
        /* One handshake message split over several records, 7 bytes each. */
        for (size_t i = 0; i < flight.len; i += 7)
            sealed_record(s, 22, flight.p + i, flight.len - i < 7 ? flight.len - i : 7);
    } else if (s->mut == M_EMPTY_HS_RECORD) {
        sealed_record(s, 22, NULL, 0);
    } else if (s->mut == M_CCS_IN_FLIGHT) {
        uint8_t v = 1;
        sealed_record(s, 22, flight.p, 10);
        plain_record(s, 20, &v, 1);
        sealed_record(s, 22, flight.p + 10, flight.len - 10);
    } else if (s->mut == M_TOO_LARGE) {
        for (size_t i = 0; i < flight.len; i += 16384)
            sealed_record(s, 22, flight.p + i, flight.len - i < 16384 ? flight.len - i : 16384);
    } else {
        size_t before = s->m->in.len;
        sealed_record(s, 22, flight.p, flight.len);
        if (s->mut == M_BAD_RECORD_MAC) s->m->in.p[before + 20] ^= 4;
    }
    buf_free(&flight);

    /* Application keys for the server side. */
    nxt__ks_application(&s->ks, s->th_after_fin);
    memcpy(s->c_ap, s->ks.client_ap, NXT__MAX_HASH);
    memcpy(s->s_ap, s->ks.server_ap, NXT__MAX_HASH);
    nxt__rprot_wipe(&s->tx);
    nxt__rprot_init(&s->tx, s->s, s->s_ap);
    s->phase = 2;
done:
    buf_free(&sh); buf_free(&exts); buf_free(&msg);
}

/* After the handshake: read what the client sent since the flight. The
 * client's Finished is checked; application data goes to `app`; the last
 * alert seen is returned (-1 if none). */
static int srv_read_client(server *s, buf *app, int *keyupdates) {
    int alert = -1;
    uint8_t type;
    const uint8_t *body;
    size_t len;
    static uint8_t rec[NXT_TLS_MAX_RECORD + 64];
    while (next_client_record(s, &type, &body, &len)) {
        if (type == 20) { buf_add(&s->ccs_seen, body, len); continue; }
        if (type == 21) {
            alert = len == 2 ? body[1] : -2;
            s->alert_level = len == 2 ? body[0] : -1;
            continue;
        }
        if (type != 23 || !s->rx.ready) { s->stray_records++; alert = -3; continue; }
        memcpy(rec, body - 5, len + 5);
        uint8_t t;
        size_t pl;
        if (nxt__rprot_open(&s->rx, rec, len + 5, &t, &pl) != NXT_TLS_OK) { alert = -4; continue; }
        const uint8_t *p = rec + 5;
        if (pl + 1 > s->max_inner) s->max_inner = pl + 1;
        if (t == 21) {
            alert = pl == 2 ? p[1] : -2;
            s->alert_level = pl == 2 ? p[0] : -1;
            continue;
        }
        if (t == 23) { if (app) buf_add(app, p, pl); continue; }
        if (t == 22) {
            size_t i = 0;
            while (i + 4 <= pl) {
                size_t ml = (size_t)p[i + 1] << 16 | (size_t)p[i + 2] << 8 | p[i + 3];
                if (p[i] == 11) {
                    s->client_sent_empty_cert = ml == 1 + 3 + 3 && p[i + 4] == 3 &&
                                                !memcmp(p + i + 5, "ctx", 3) &&
                                                p[i + 8] == 0 && p[i + 9] == 0 && p[i + 10] == 0;
                    th_add(s, p + i, 4 + ml);
                } else if (p[i] == 20) {
                    uint8_t th[NXT__MAX_HASH], mac[NXT__MAX_HASH];
                    th_now(s, th);
                    nxt__finished_mac(s->s, s->c_hs, th, mac);
                    s->client_finished_ok = ml == s->s->hash_len && !memcmp(p + i + 4, mac, ml);
                    nxt__rprot_wipe(&s->rx);
                    nxt__rprot_init(&s->rx, s->s, s->c_ap);
                } else if (p[i] == 24) {
                    if (keyupdates) (*keyupdates)++;
                    nxt__next_traffic_secret(s->s, s->c_ap);
                    nxt__rprot_wipe(&s->rx);
                    nxt__rprot_init(&s->rx, s->s, s->c_ap);
                }
                i += 4 + ml;
            }
        }
    }
    return alert;
}

/* Server -> client after the handshake. */
static void srv_send(server *s, uint8_t type, const void *p, size_t n) { sealed_record(s, type, p, n); }

static void srv_key_update(server *s, int request) {
    uint8_t ku[5] = { 24, 0, 0, 1, (uint8_t)request };
    sealed_record(s, 22, ku, 5);
    nxt__next_traffic_secret(s->s, s->s_ap);
    nxt__rprot_wipe(&s->tx);
    nxt__rprot_init(&s->tx, s->s, s->s_ap);
}

/* ------------------------------------------------------------------ */
/* Test PKI                                                             */
/* ------------------------------------------------------------------ */

static const uint8_t ROOT_SEED[32] = { 1 }, INT_SEED[32] = { 2 }, LEAF_SEED[32] = { 3 },
                     OTHER_SEED[32] = { 4 };
static uint8_t g_root[1024], g_int[1024], g_leaf[1024], g_leaf_direct[1024], g_self[1024],
    g_expired[1024];
static size_t g_root_len, g_int_len, g_leaf_len, g_leaf_direct_len, g_self_len, g_expired_len;
static nxt_x509_cert g_anchor;
static nxt_trust_store g_roots;
static const int64_t NOW = 1767225600;            /* 2026-01-01 */

static void make_pki(void) {
    certgen_spec root = { "Test Root", "Test Root", ROOT_SEED, ROOT_SEED, 1, NULL,
                          "250101000000Z", "350101000000Z", 1 };
    certgen_spec inter = { "Test Intermediate", "Test Root", INT_SEED, ROOT_SEED, 1, NULL,
                           "250101000000Z", "330101000000Z", 2 };
    certgen_spec leaf = { "test.example", "Test Intermediate", LEAF_SEED, INT_SEED, 0,
                          "test.example", "250601000000Z", "270101000000Z", 3 };
    certgen_spec direct = { "test.example", "Test Root", LEAF_SEED, ROOT_SEED, 0,
                            "test.example", "250601000000Z", "270101000000Z", 4 };
    certgen_spec self = { "test.example", "test.example", LEAF_SEED, LEAF_SEED, 0,
                          "test.example", "250601000000Z", "270101000000Z", 5 };
    certgen_spec expired = { "test.example", "Test Root", LEAF_SEED, ROOT_SEED, 0,
                             "test.example", "240101000000Z", "251201000000Z", 6 };
    g_root_len = certgen_make(&root, g_root, sizeof(g_root));
    g_int_len = certgen_make(&inter, g_int, sizeof(g_int));
    g_leaf_len = certgen_make(&leaf, g_leaf, sizeof(g_leaf));
    g_leaf_direct_len = certgen_make(&direct, g_leaf_direct, sizeof(g_leaf_direct));
    g_self_len = certgen_make(&self, g_self, sizeof(g_self));
    g_expired_len = certgen_make(&expired, g_expired, sizeof(g_expired));
    CHECK(nxt_x509_parse(&g_anchor, g_root, g_root_len) == NXT_X509_OK, "test root parses");
    g_roots.anchors = &g_anchor;
    g_roots.count = 1;
    (void)OTHER_SEED;
}

/* ------------------------------------------------------------------ */
/* Running one scripted handshake                                       */
/* ------------------------------------------------------------------ */

typedef struct {
    mock m;
    server s;
    unsigned ctr;
    nxt_tls_config cfg;
    int rc;
} run;

static void run_init(run *r, int mut, uint16_t suite) {
    memset(r, 0, sizeof(*r));
    r->s.mut = mut;
    r->s.m = &r->m;
    r->s.suite = suite;
    memcpy(r->s.leaf_seed, LEAF_SEED, 32);
    r->s.chain[0] = g_leaf;
    r->s.chain_len[0] = g_leaf_len;
    r->s.chain[1] = g_int;
    r->s.chain_len[1] = g_int_len;
    r->s.nchain = 2;
    r->m.srv = &r->s;
    nxt_tls_config cfg = { "test.example", &g_roots, NOW, { &r->m, m_send, m_recv },
                           fake_random, &r->ctr, NULL };
    r->cfg = cfg;
}

static int run_connect(run *r) {
    memset(&g_conn, 0xa5, sizeof(g_conn));            /* the client must not rely on zeroed memory */
    nxt__tls_test_hello(&g_conn, NULL, 0, NULL);      /* no test hooks: the real client */
    r->rc = nxt_tls_connect(&g_conn, &r->cfg);
    return r->rc;
}

static void run_free(run *r) {
    nxt_tls_close(&g_conn);
    buf_free(&r->m.in);
    buf_free(&r->m.out);
    buf_free(&r->s.ccs_seen);
    nxt__ks_wipe(&r->s.ks);
}

/* A handshake that must fail with err and send alert (-1: no alert). */
static void expect_fail(const char *what, int mut, int err, int alert) {
    run r;
    run_init(&r, mut, NXT_TLS_AES_128_GCM_SHA256);
    run_connect(&r);
    const nxt_tls_info *in = nxt_tls_get_info(&g_conn);
    int wire = srv_read_client(&r.s, NULL, NULL);
    CHECK(r.rc == err, "%s: got %s, want %s", what, nxt_tls_err_str(r.rc), nxt_tls_err_str(err));
    CHECK(in->alert_sent == alert && wire == alert, "%s: alert sent %d (on the wire %d), want %d",
          what, in->alert_sent, wire, alert);
    CHECK(alert < 0 || r.s.alert_level == 2, "%s: alert level %d, want fatal", what, r.s.alert_level);
    CHECK(r.s.stray_records == 0, "%s: client sent %d unexpected records", what, r.s.stray_records);
    /* A failed connection refuses further use. */
    uint8_t b[4];
    CHECK(nxt_tls_read(&g_conn, b, 4) == NXT_TLS_ERR_ARG &&
          nxt_tls_write(&g_conn, b, 4) == NXT_TLS_ERR_ARG, "%s: usable after failure", what);
    run_free(&r);
}

static void test_good_handshakes(void) {
    static const uint16_t suites[] = { NXT_TLS_AES_128_GCM_SHA256, NXT_TLS_AES_256_GCM_SHA384,
                                       NXT_TLS_CHACHA20_POLY1305_SHA256 };
    for (size_t i = 0; i < 3; i++) {
        for (size_t chunk = 0; chunk <= 1; chunk++) {
            run r;
            run_init(&r, M_NONE, suites[i]);
            r.m.chunk = chunk;
            r.cfg.alpn = "http/1.1";
            r.s.alpn = "http/1.1";
            int rc = run_connect(&r);
            const nxt_tls_info *in = nxt_tls_get_info(&g_conn);
            CHECK(rc == NXT_TLS_OK, "suite %04x chunk %zu: connect: %s (cert %s, alert %d)",
                  suites[i], chunk, nxt_tls_err_str(rc), nxt_x509_err_str(in->cert_error),
                  in->alert_sent);
            if (rc != NXT_TLS_OK) { run_free(&r); continue; }
            CHECK(in->suite == suites[i] && !strcmp(in->alpn, "http/1.1") && in->chain_len == 2 &&
                  in->path_len >= 2 && !in->hello_retry, "suite %04x: info", suites[i]);
            /* Compatibility mode: 32-byte session id and one change_cipher_spec. */
            CHECK(r.s.sid_len == 32 && r.s.ccs_seen.len == 0, "session id");
            CHECK(r.s.ch_had_rsl && r.s.ch_rsl == 16385, "record_size_limit not offered as 2^14+1");
            buf app = {0};
            CHECK(srv_read_client(&r.s, &app, NULL) == -1 && r.s.client_finished_ok &&
                  r.s.ccs_seen.len == 1, "suite %04x: client Finished not valid", suites[i]);

            /* Data both ways, a ticket, a large write split into records. */
            static uint8_t big[40000];
            for (size_t k = 0; k < sizeof(big); k++) big[k] = (uint8_t)(k * 13);
            CHECK(nxt_tls_write(&g_conn, big, sizeof(big)) == (long)sizeof(big), "big write");
            srv_read_client(&r.s, &app, NULL);
            CHECK(app.len == sizeof(big) && !memcmp(app.p, big, sizeof(big)), "big write arrives");
            uint8_t nst[] = { 4, 0, 0, 14, 0, 0, 0, 60, 1, 2, 3, 4, 0, 0, 1, 'T', 0, 0 };
            srv_send(&r.s, 22, nst, sizeof(nst));
            srv_send(&r.s, 23, "pong", 4);
            uint8_t got[64];
            long k = nxt_tls_read(&g_conn, got, sizeof(got));
            CHECK(k == 4 && !memcmp(got, "pong", 4), "read after ticket");
            buf_free(&app);
            run_free(&r);
        }
    }
    printf("  scripted server: 3 suites, whole records and 1 byte per read, ALPN, ticket, 40 KB write\n");
}

static void test_key_update(void) {
    run r;
    run_init(&r, M_NONE, NXT_TLS_CHACHA20_POLY1305_SHA256);
    CHECK(run_connect(&r) == NXT_TLS_OK, "key update: connect");
    buf app = {0};
    int kus = 0;
    srv_read_client(&r.s, &app, &kus);
    /* Server updates its key and asks the client to update too. */
    srv_key_update(&r.s, 1);
    srv_send(&r.s, 23, "after", 5);
    uint8_t got[16];
    long k = nxt_tls_read(&g_conn, got, sizeof(got));
    CHECK(k == 5 && !memcmp(got, "after", 5), "key update: data after the server's update");
    CHECK(nxt_tls_write(&g_conn, (const uint8_t *)"new", 3) == 3, "key update: write");
    srv_read_client(&r.s, &app, &kus);
    CHECK(kus == 1 && app.len == 3 && !memcmp(app.p, "new", 3),
          "key update: client did not answer with its own KeyUpdate (%d)", kus);
    /* An update that does not ask for one back. */
    srv_key_update(&r.s, 0);
    srv_send(&r.s, 23, "x", 1);
    CHECK(nxt_tls_read(&g_conn, got, sizeof(got)) == 1, "key update: no request");
    srv_read_client(&r.s, &app, &kus);
    CHECK(kus == 1, "key update: answered an update that asked for nothing");
    /* request_update 2 is illegal. */
    uint8_t bad[5] = { 24, 0, 0, 1, 2 };
    srv_send(&r.s, 22, bad, 5);
    CHECK(nxt_tls_read(&g_conn, got, sizeof(got)) == NXT_TLS_ERR_ILLEGAL_PARAMETER &&
          srv_read_client(&r.s, NULL, NULL) == NXT_TLS_ALERT_ILLEGAL_PARAMETER,
          "key update: request_update 2 accepted");
    buf_free(&app);
    run_free(&r);
}

static void test_post_handshake(void) {
    uint8_t got[16];
    {
        /* close_notify ends reading; writing still works (half-close). */
        run r;
        run_init(&r, M_NONE, NXT_TLS_AES_128_GCM_SHA256);
        run_connect(&r);
        srv_read_client(&r.s, NULL, NULL);
        uint8_t cn[2] = { 1, 0 };
        srv_send(&r.s, 21, cn, 2);
        CHECK(nxt_tls_read(&g_conn, got, sizeof(got)) == 0 &&
              nxt_tls_read(&g_conn, got, sizeof(got)) == 0, "close_notify not end of data");
        CHECK(nxt_tls_write(&g_conn, (const uint8_t *)"bye", 3) == 3, "write after close_notify");
        CHECK(nxt_tls_close(&g_conn) == NXT_TLS_OK, "close");
        buf app = {0};
        CHECK(srv_read_client(&r.s, &app, NULL) == 0 && app.len == 3 && r.s.alert_level == 1,
              "client close_notify (level %d)", r.s.alert_level);
        buf_free(&app);
        run_free(&r);
    }
    {
        /* Transport ends without close_notify: truncation is reported. */
        run r;
        run_init(&r, M_NONE, NXT_TLS_AES_128_GCM_SHA256);
        run_connect(&r);
        srv_send(&r.s, 23, "part", 4);
        CHECK(nxt_tls_read(&g_conn, got, sizeof(got)) == 4, "data before truncation");
        CHECK(nxt_tls_read(&g_conn, got, sizeof(got)) == NXT_TLS_ERR_CLOSED, "truncation not reported");
        run_free(&r);
    }
    {
        /* A fatal alert from the server. */
        run r;
        run_init(&r, M_NONE, NXT_TLS_AES_128_GCM_SHA256);
        run_connect(&r);
        uint8_t al[2] = { 2, 80 };
        srv_send(&r.s, 21, al, 2);
        CHECK(nxt_tls_read(&g_conn, got, sizeof(got)) == NXT_TLS_ERR_ALERT &&
              nxt_tls_get_info(&g_conn)->alert_received == 80, "server alert");
        run_free(&r);
    }
    {
        /* A CertificateRequest after the handshake was never allowed. */
        run r;
        run_init(&r, M_NONE, NXT_TLS_AES_128_GCM_SHA256);
        run_connect(&r);
        srv_read_client(&r.s, NULL, NULL);
        uint8_t cr[] = { 13, 0, 0, 9, 0, 0, 6, 0, 13, 0, 2, 8, 7 };
        srv_send(&r.s, 22, cr, sizeof(cr));
        CHECK(nxt_tls_read(&g_conn, got, sizeof(got)) == NXT_TLS_ERR_UNEXPECTED_MESSAGE &&
              srv_read_client(&r.s, NULL, NULL) == NXT_TLS_ALERT_UNEXPECTED_MESSAGE,
              "post-handshake CertificateRequest");
        run_free(&r);
    }
    {
        /* A tampered application record. */
        run r;
        run_init(&r, M_NONE, NXT_TLS_AES_128_GCM_SHA256);
        run_connect(&r);
        srv_read_client(&r.s, NULL, NULL);
        size_t at = r.m.in.len;
        srv_send(&r.s, 23, "data", 4);
        r.m.in.p[at + 7] ^= 1;
        CHECK(nxt_tls_read(&g_conn, got, sizeof(got)) == NXT_TLS_ERR_BAD_RECORD_MAC &&
              srv_read_client(&r.s, NULL, NULL) == NXT_TLS_ALERT_BAD_RECORD_MAC,
              "tampered application record");
        run_free(&r);
    }
    printf("  post-handshake: KeyUpdate both ways, close_notify, truncation, alerts, tampering\n");
}

/* Record size limit and message framing after the handshake. */
static void test_limits_and_framing(void) {
    uint8_t got[64];
    {
        /* The server allows 64-byte records (63 bytes of content): writes are split. */
        run r;
        run_init(&r, M_EE_RECORD_LIMIT, NXT_TLS_CHACHA20_POLY1305_SHA256);
        CHECK(run_connect(&r) == NXT_TLS_OK, "record_size_limit 64: connect: %s",
              nxt_tls_err_str(r.rc));
        buf app = {0};
        srv_read_client(&r.s, &app, NULL);
        r.s.max_inner = 0;
        static uint8_t data[1000];
        for (size_t k = 0; k < sizeof(data); k++) data[k] = (uint8_t)(k * 7);
        CHECK(nxt_tls_write(&g_conn, data, sizeof(data)) == (long)sizeof(data),
              "record_size_limit 64: write");
        srv_read_client(&r.s, &app, NULL);
        CHECK(app.len == sizeof(data) && !memcmp(app.p, data, sizeof(data)) &&
              r.s.max_inner == 64, "record_size_limit 64: largest record %zu, want 64",
              r.s.max_inner);
        buf_free(&app);
        run_free(&r);
    }
    {
        /* A ticket with an empty ticket field is malformed. */
        run r;
        run_init(&r, M_NONE, NXT_TLS_AES_128_GCM_SHA256);
        run_connect(&r);
        srv_read_client(&r.s, NULL, NULL);
        uint8_t nst[] = { 4, 0, 0, 13, 0, 0, 0, 60, 1, 2, 3, 4, 0, 0, 0, 0, 0 };
        srv_send(&r.s, 22, nst, sizeof(nst));
        CHECK(nxt_tls_read(&g_conn, got, sizeof(got)) == NXT_TLS_ERR_DECODE &&
              srv_read_client(&r.s, NULL, NULL) == NXT_TLS_ALERT_DECODE_ERROR,
              "empty session ticket accepted");
        run_free(&r);
    }
    {
        /* KeyUpdate must end its record: the next message is under the new key. */
        run r;
        run_init(&r, M_NONE, NXT_TLS_AES_128_GCM_SHA256);
        run_connect(&r);
        srv_read_client(&r.s, NULL, NULL);
        uint8_t m[5 + 18] = { 24, 0, 0, 1, 0, 4, 0, 0, 14, 0, 0, 0, 60, 1, 2, 3, 4, 0, 0, 1, 'T', 0, 0 };
        srv_send(&r.s, 22, m, sizeof(m));
        CHECK(nxt_tls_read(&g_conn, got, sizeof(got)) == NXT_TLS_ERR_UNEXPECTED_MESSAGE &&
              srv_read_client(&r.s, NULL, NULL) == NXT_TLS_ALERT_UNEXPECTED_MESSAGE,
              "KeyUpdate sharing its record accepted");
        run_free(&r);
    }
    {
        /* Application data in the middle of a fragmented handshake message. */
        run r;
        run_init(&r, M_NONE, NXT_TLS_AES_128_GCM_SHA256);
        run_connect(&r);
        srv_read_client(&r.s, NULL, NULL);
        uint8_t nst[] = { 4, 0, 0, 14, 0, 0, 0, 60, 1, 2, 3, 4, 0, 0, 1, 'T', 0, 0 };
        srv_send(&r.s, 22, nst, 6);
        srv_send(&r.s, 23, "mid", 3);
        srv_send(&r.s, 22, nst + 6, sizeof(nst) - 6);
        CHECK(nxt_tls_read(&g_conn, got, sizeof(got)) == NXT_TLS_ERR_UNEXPECTED_MESSAGE &&
              srv_read_client(&r.s, NULL, NULL) == NXT_TLS_ALERT_UNEXPECTED_MESSAGE,
              "application data inside a handshake message accepted");
        run_free(&r);
    }
    {
        /* The same ticket split over two records is fine. */
        run r;
        run_init(&r, M_NONE, NXT_TLS_AES_128_GCM_SHA256);
        run_connect(&r);
        uint8_t nst[] = { 4, 0, 0, 14, 0, 0, 0, 60, 1, 2, 3, 4, 0, 0, 1, 'T', 0, 0 };
        srv_send(&r.s, 22, nst, 6);
        srv_send(&r.s, 22, nst + 6, sizeof(nst) - 6);
        srv_send(&r.s, 23, "ok", 2);
        CHECK(nxt_tls_read(&g_conn, got, sizeof(got)) == 2, "ticket split over two records");
        run_free(&r);
    }
    printf("  record_size_limit, ticket and KeyUpdate framing\n");
}

static void test_hrr_and_cr(void) {
    run r;
    run_init(&r, M_HRR_COOKIE, NXT_TLS_AES_256_GCM_SHA384);
    int rc = run_connect(&r);
    CHECK(rc == NXT_TLS_OK && nxt_tls_get_info(&g_conn)->hello_retry, "HRR cookie: %s",
          nxt_tls_err_str(rc));
    CHECK(r.s.ch_had_cookie && r.s.ch2_same, "HRR: second ClientHello lacks the cookie or changed");
    CHECK(srv_read_client(&r.s, NULL, NULL) == -1 && r.s.client_finished_ok, "HRR: client Finished");
    run_free(&r);

    run_init(&r, M_CR, NXT_TLS_AES_128_GCM_SHA256);
    rc = run_connect(&r);
    CHECK(rc == NXT_TLS_OK && nxt_tls_get_info(&g_conn)->cert_requested, "CertificateRequest: %s",
          nxt_tls_err_str(rc));
    CHECK(srv_read_client(&r.s, NULL, NULL) == -1 && r.s.client_sent_empty_cert &&
          r.s.client_finished_ok, "CertificateRequest: empty Certificate and Finished");
    run_free(&r);

    /* Flight split over many records, and a CCS between handshake records. */
    run_init(&r, M_FLIGHT_SPLIT, NXT_TLS_AES_128_GCM_SHA256);
    CHECK(run_connect(&r) == NXT_TLS_OK, "flight split over 7-byte records: %s",
          nxt_tls_err_str(r.rc));
    run_free(&r);
    run_init(&r, M_CCS_IN_FLIGHT, NXT_TLS_AES_128_GCM_SHA256);
    CHECK(run_connect(&r) == NXT_TLS_OK, "change_cipher_spec inside the flight: %s",
          nxt_tls_err_str(r.rc));
    run_free(&r);
    printf("  HelloRetryRequest with cookie, CertificateRequest, split flight, CCS in flight\n");
}

/* Certificates: each refusal with its X.509 reason and alert. */
static void cert_case(const char *what, const uint8_t *const *ders, const size_t *lens, int n,
                      const char *host, int64_t now, const nxt_trust_store *roots, int want_rc,
                      int want_x509, int want_alert, int want_index) {
    run r;
    run_init(&r, M_NONE, NXT_TLS_AES_128_GCM_SHA256);
    for (int i = 0; i < n; i++) { r.s.chain[i] = (uint8_t *)ders[i]; r.s.chain_len[i] = lens[i]; }
    r.s.nchain = n;
    r.cfg.host = host;
    r.cfg.now = now;
    r.cfg.roots = roots;
    run_connect(&r);
    const nxt_tls_info *in = nxt_tls_get_info(&g_conn);
    int wire = srv_read_client(&r.s, NULL, NULL);
    CHECK(r.rc == want_rc, "%s: got %s", what, nxt_tls_err_str(r.rc));
    if (want_rc == NXT_TLS_ERR_CERTIFICATE)
        CHECK(in->cert_error == want_x509 && in->alert_sent == want_alert && wire == want_alert &&
              in->cert_failed_index == want_index,
              "%s: reason %s, alert %d (wire %d), index %d", what, nxt_x509_err_str(in->cert_error),
              in->alert_sent, wire, in->cert_failed_index);
    if (want_rc == NXT_TLS_ERR_CERTIFICATE)
        printf("  refused %-34s %s (alert %s)\n", what, nxt_x509_err_str(in->cert_error),
               nxt_tls_alert_str(in->alert_sent));
    run_free(&r);
}

static void test_certificates(void) {
    nxt_trust_store empty = { NULL, 0 };
    static uint8_t junk[40] = { 0x30, 0x26, 1, 2, 3 };
    const uint8_t *chain[MAX_CHAIN];
    size_t lens[MAX_CHAIN];

    chain[0] = g_leaf_direct; lens[0] = g_leaf_direct_len;
    cert_case("leaf signed by the root", chain, lens, 1, "test.example", NOW, &g_roots,
              NXT_TLS_OK, 0, -1, -1);
    chain[1] = junk; lens[1] = sizeof(junk);
    cert_case("unreadable extra certificate", chain, lens, 2, "TEST.example", NOW, &g_roots,
              NXT_TLS_OK, 0, -1, -1);
    chain[0] = g_leaf; lens[0] = g_leaf_len; chain[1] = g_int; lens[1] = g_int_len;
    cert_case("wrong host", chain, lens, 2, "evil.example", NOW, &g_roots,
              NXT_TLS_ERR_CERTIFICATE, NXT_X509_ERR_HOST_MISMATCH,
              NXT_TLS_ALERT_CERTIFICATE_UNKNOWN, 0);
    cert_case("whole chain expired (2033)", chain, lens, 2, "test.example", 1990000000, &g_roots,
              NXT_TLS_ERR_CERTIFICATE, NXT_X509_ERR_EXPIRED, NXT_TLS_ALERT_CERTIFICATE_EXPIRED, 0);
    chain[0] = g_expired; lens[0] = g_expired_len;
    cert_case("expired certificate", chain, lens, 1, "test.example", NOW, &g_roots,
              NXT_TLS_ERR_CERTIFICATE, NXT_X509_ERR_EXPIRED, NXT_TLS_ALERT_CERTIFICATE_EXPIRED, 0);
    chain[0] = g_self; lens[0] = g_self_len;
    cert_case("self-signed certificate", chain, lens, 1, "test.example", NOW, &g_roots,
              NXT_TLS_ERR_CERTIFICATE, NXT_X509_ERR_UNKNOWN_ISSUER, NXT_TLS_ALERT_UNKNOWN_CA, 0);
    chain[0] = g_leaf; lens[0] = g_leaf_len;
    cert_case("untrusted root", chain, lens, 2, "test.example", NOW, &empty,
              NXT_TLS_ERR_CERTIFICATE, NXT_X509_ERR_UNKNOWN_ISSUER, NXT_TLS_ALERT_UNKNOWN_CA, 1);
    cert_case("missing intermediate", chain, lens, 1, "test.example", NOW, &g_roots,
              NXT_TLS_ERR_CERTIFICATE, NXT_X509_ERR_UNKNOWN_ISSUER, NXT_TLS_ALERT_UNKNOWN_CA, 0);
    chain[0] = junk; lens[0] = sizeof(junk);
    cert_case("unreadable certificate", chain, lens, 1, "test.example", NOW, &g_roots,
              NXT_TLS_ERR_CERTIFICATE, NXT_X509_ERR_ENCODING, NXT_TLS_ALERT_BAD_CERTIFICATE, 0);
    chain[0] = g_root; lens[0] = g_root_len;
    cert_case("CA certificate as server", chain, lens, 1, "test.example", NOW, &g_roots,
              NXT_TLS_ERR_CERTIFICATE, NXT_X509_ERR_LEAF_IS_CA,
              NXT_TLS_ALERT_CERTIFICATE_UNKNOWN, 0);
    for (int i = 0; i < 17; i++) { chain[i] = i ? g_int : g_leaf; lens[i] = i ? g_int_len : g_leaf_len; }
    cert_case("17 certificates", chain, lens, 17, "test.example", NOW, &g_roots,
              NXT_TLS_ERR_CERTIFICATE, NXT_X509_ERR_CHAIN_TOO_LONG,
              NXT_TLS_ALERT_CERTIFICATE_UNKNOWN, 16);
    /* An IP literal host is checked against iPAddress SANs (none here) and sends no SNI. */
    chain[0] = g_leaf; chain[1] = g_int;
    cert_case("IP host vs DNS certificate", chain, lens, 2, "192.0.2.1", NOW, &g_roots,
              NXT_TLS_ERR_CERTIFICATE, NXT_X509_ERR_HOST_MISMATCH,
              NXT_TLS_ALERT_CERTIFICATE_UNKNOWN, 0);
}

static void test_protocol_errors(void) {
    expect_fail("TLS 1.2 ServerHello", M_SH_TLS12, NXT_TLS_ERR_PROTOCOL_VERSION,
                NXT_TLS_ALERT_PROTOCOL_VERSION);
    expect_fail("supported_versions selects TLS 1.2", M_SH_SV_TLS12,
                NXT_TLS_ERR_ILLEGAL_PARAMETER, NXT_TLS_ALERT_ILLEGAL_PARAMETER);
    expect_fail("legacy_version 0x0304", M_SH_VERSION_13_LEGACY, NXT_TLS_ERR_ILLEGAL_PARAMETER,
                NXT_TLS_ALERT_ILLEGAL_PARAMETER);
    expect_fail("session id not echoed", M_SH_BAD_SID, NXT_TLS_ERR_ILLEGAL_PARAMETER,
                NXT_TLS_ALERT_ILLEGAL_PARAMETER);
    expect_fail("suite not offered", M_SH_SUITE_NOT_OFFERED, NXT_TLS_ERR_ILLEGAL_PARAMETER,
                NXT_TLS_ALERT_ILLEGAL_PARAMETER);
    expect_fail("compression method 1", M_SH_COMPRESSION, NXT_TLS_ERR_ILLEGAL_PARAMETER,
                NXT_TLS_ALERT_ILLEGAL_PARAMETER);
    expect_fail("low-order X25519 share", M_SH_LOW_ORDER, NXT_TLS_ERR_ILLEGAL_PARAMETER,
                NXT_TLS_ALERT_ILLEGAL_PARAMETER);
    expect_fail("P-256 share", M_SH_GROUP_P256, NXT_TLS_ERR_ILLEGAL_PARAMETER,
                NXT_TLS_ALERT_ILLEGAL_PARAMETER);
    expect_fail("no key_share", M_SH_NO_KEY_SHARE, NXT_TLS_ERR_MISSING_EXTENSION,
                NXT_TLS_ALERT_MISSING_EXTENSION);
    expect_fail("duplicate extension", M_SH_DUP_EXT, NXT_TLS_ERR_ILLEGAL_PARAMETER,
                NXT_TLS_ALERT_ILLEGAL_PARAMETER);
    expect_fail("ALPN in ServerHello (not offered)", M_SH_ALPN,
                NXT_TLS_ERR_UNSUPPORTED_EXTENSION, NXT_TLS_ALERT_UNSUPPORTED_EXTENSION);
    expect_fail("truncated ServerHello", M_SH_TRUNCATED, NXT_TLS_ERR_DECODE,
                NXT_TLS_ALERT_DECODE_ERROR);
    expect_fail("handshake_failure alert", M_PLAIN_ALERT, NXT_TLS_ERR_ALERT, -1);
    expect_fail("connection closed after ServerHello", M_EOF_AFTER_SH, NXT_TLS_ERR_CLOSED,
                -1);
    expect_fail("ServerHello and EE in one record", M_SH_AND_EE_ONE_RECORD,
                NXT_TLS_ERR_UNEXPECTED_MESSAGE, NXT_TLS_ALERT_UNEXPECTED_MESSAGE);
    expect_fail("change_cipher_spec value 2", M_CCS_BAD, NXT_TLS_ERR_UNEXPECTED_MESSAGE,
                NXT_TLS_ALERT_UNEXPECTED_MESSAGE);
    expect_fail("plaintext application data", M_PLAIN_APPDATA,
                NXT_TLS_ERR_UNEXPECTED_MESSAGE, NXT_TLS_ALERT_UNEXPECTED_MESSAGE);
    expect_fail("record of 2^14 + 1 bytes", M_RECORD_OVERFLOW, NXT_TLS_ERR_RECORD_OVERFLOW,
                NXT_TLS_ALERT_RECORD_OVERFLOW);
    expect_fail("HRR asking for a group", M_HRR_KEY_SHARE, NXT_TLS_ERR_ILLEGAL_PARAMETER,
                NXT_TLS_ALERT_ILLEGAL_PARAMETER);
    expect_fail("HRR changing nothing", M_HRR_EMPTY, NXT_TLS_ERR_ILLEGAL_PARAMETER,
                NXT_TLS_ALERT_ILLEGAL_PARAMETER);
    expect_fail("second HRR", M_HRR_TWICE, NXT_TLS_ERR_UNEXPECTED_MESSAGE,
                NXT_TLS_ALERT_UNEXPECTED_MESSAGE);
    expect_fail("suite changed after HRR", M_HRR_SUITE_CHANGE, NXT_TLS_ERR_ILLEGAL_PARAMETER,
                NXT_TLS_ALERT_ILLEGAL_PARAMETER);
    expect_fail("unsolicited extension in EE", M_EE_UNSOLICITED,
                NXT_TLS_ERR_UNSUPPORTED_EXTENSION, NXT_TLS_ALERT_UNSUPPORTED_EXTENSION);
    expect_fail("key_share in EE", M_EE_KEY_SHARE, NXT_TLS_ERR_ILLEGAL_PARAMETER,
                NXT_TLS_ALERT_ILLEGAL_PARAMETER);
    expect_fail("ALPN not offered", M_EE_WRONG_ALPN, NXT_TLS_ERR_UNSUPPORTED_EXTENSION,
                NXT_TLS_ALERT_UNSUPPORTED_EXTENSION);
    expect_fail("record_size_limit 63", M_EE_RECORD_LIMIT_63, NXT_TLS_ERR_ILLEGAL_PARAMETER,
                NXT_TLS_ALERT_ILLEGAL_PARAMETER);
    expect_fail("no EncryptedExtensions", M_NO_EE, NXT_TLS_ERR_UNEXPECTED_MESSAGE,
                NXT_TLS_ALERT_UNEXPECTED_MESSAGE);
    expect_fail("CertificateRequest without signature_algorithms", M_CR_NO_SIGALGS,
                NXT_TLS_ERR_MISSING_EXTENSION, NXT_TLS_ALERT_MISSING_EXTENSION);
    expect_fail("empty certificate list", M_CERT_EMPTY, NXT_TLS_ERR_DECODE,
                NXT_TLS_ALERT_DECODE_ERROR);
    expect_fail("certificate request context", M_CERT_CONTEXT, NXT_TLS_ERR_ILLEGAL_PARAMETER,
                NXT_TLS_ALERT_ILLEGAL_PARAMETER);
    expect_fail("OCSP in certificate entry", M_CERT_ENTRY_EXT, NXT_TLS_ERR_UNSUPPORTED_EXTENSION,
                NXT_TLS_ALERT_UNSUPPORTED_EXTENSION);
    expect_fail("bad CertificateVerify signature", M_CV_BAD_SIG, NXT_TLS_ERR_BAD_SIGNATURE,
                NXT_TLS_ALERT_DECRYPT_ERROR);
    expect_fail("PKCS#1 CertificateVerify", M_CV_PKCS1, NXT_TLS_ERR_ILLEGAL_PARAMETER,
                NXT_TLS_ALERT_ILLEGAL_PARAMETER);
    expect_fail("ECDSA scheme with an Ed25519 key", M_CV_WRONG_TYPE,
                NXT_TLS_ERR_ILLEGAL_PARAMETER, NXT_TLS_ALERT_ILLEGAL_PARAMETER);
    expect_fail("no CertificateVerify", M_NO_CV, NXT_TLS_ERR_UNEXPECTED_MESSAGE,
                NXT_TLS_ALERT_UNEXPECTED_MESSAGE);
    expect_fail("bad server Finished", M_FIN_BAD, NXT_TLS_ERR_BAD_FINISHED,
                NXT_TLS_ALERT_DECRYPT_ERROR);
    expect_fail("short server Finished", M_FIN_SHORT, NXT_TLS_ERR_DECODE,
                NXT_TLS_ALERT_DECODE_ERROR);
    expect_fail("long server Finished", M_FIN_LONG, NXT_TLS_ERR_DECODE,
                NXT_TLS_ALERT_DECODE_ERROR);
    expect_fail("application data before Finished", M_APP_BEFORE_FIN,
                NXT_TLS_ERR_UNEXPECTED_MESSAGE, NXT_TLS_ALERT_UNEXPECTED_MESSAGE);
    expect_fail("tampered handshake record", M_BAD_RECORD_MAC, NXT_TLS_ERR_BAD_RECORD_MAC,
                NXT_TLS_ALERT_BAD_RECORD_MAC);
    expect_fail("empty handshake record", M_EMPTY_HS_RECORD, NXT_TLS_ERR_UNEXPECTED_MESSAGE,
                NXT_TLS_ALERT_UNEXPECTED_MESSAGE);
    expect_fail("unknown message before Finished", M_UNKNOWN_HS_MESSAGE,
                NXT_TLS_ERR_UNEXPECTED_MESSAGE, NXT_TLS_ALERT_UNEXPECTED_MESSAGE);
    expect_fail("certificate message over the size limit", M_TOO_LARGE, NXT_TLS_ERR_TOO_LARGE,
                NXT_TLS_ALERT_INTERNAL_ERROR);
    printf("  scripted server: protocol violations refused with the right alert\n");
}

static void test_args(void) {
    run r;
    run_init(&r, M_NONE, NXT_TLS_AES_128_GCM_SHA256);
    nxt_tls_config cfg = r.cfg;
    CHECK(nxt_tls_connect(NULL, &cfg) == NXT_TLS_ERR_ARG, "NULL conn");
    CHECK(nxt_tls_connect(&g_conn, NULL) == NXT_TLS_ERR_ARG, "NULL config");
    cfg.host = NULL;
    CHECK(nxt_tls_connect(&g_conn, &cfg) == NXT_TLS_ERR_ARG, "NULL host");
    cfg.host = "bad_host.example";
    CHECK(nxt_tls_connect(&g_conn, &cfg) == NXT_TLS_ERR_ARG, "invalid host name");
    cfg.host = "trailing.dot.";
    CHECK(nxt_tls_connect(&g_conn, &cfg) == NXT_TLS_ERR_ARG, "trailing dot");
    cfg.host = "test.example";
    cfg.alpn = "";
    CHECK(nxt_tls_connect(&g_conn, &cfg) == NXT_TLS_ERR_ARG, "empty ALPN");
    cfg.alpn = "0123456789012345678901234567890123";
    CHECK(nxt_tls_connect(&g_conn, &cfg) == NXT_TLS_ERR_ARG, "long ALPN");
    cfg.alpn = NULL;
    cfg.random = failing_random;
    CHECK(nxt_tls_connect(&g_conn, &cfg) == NXT_TLS_ERR_RANDOM && r.m.out.len == 0,
          "random failure: nothing must be sent");
    CHECK(nxt_tls_get_info(&g_conn)->alert_sent == -1, "random failure: alert");
    cfg.random = fake_random;
    cfg.roots = NULL;
    CHECK(nxt_tls_connect(&g_conn, &cfg) == NXT_TLS_ERR_ARG, "NULL roots");
    uint8_t b[4];
    memset(&g_conn, 0, sizeof(g_conn));
    CHECK(nxt_tls_read(&g_conn, b, 4) == NXT_TLS_ERR_ARG && nxt_tls_write(&g_conn, b, 4) ==
          NXT_TLS_ERR_ARG && nxt_tls_close(&g_conn) == NXT_TLS_ERR_ARG,
          "unconnected memory accepted");
    CHECK(nxt_tls_get_info(NULL)->alert_sent == -1, "info for NULL");
    run_free(&r);
}

int main(int argc, char **argv) {
    if (argc != 2) {
        printf("usage: %s <vector dir>\n", argv[0]);
        return 2;
    }
    char *trace = vec_read_file(argv[1], "rfc8448/transcripts.vec");
    vec_parse(trace, trace_cb, NULL);

    replay_trace("Simple_1RTT_Handshake", 0, 0);
    replay_trace("Simple_1RTT_Handshake", 1, 0);
    replay_trace("Middlebox_Compatibility_Mode", 0, 0);
    replay_trace("Client_Authentication_Handshake", 0, 1);
    replay_trace_real_cert();

    make_pki();
    test_good_handshakes();
    test_hrr_and_cr();
    test_key_update();
    test_post_handshake();
    test_limits_and_framing();
    test_certificates();
    test_protocol_errors();
    test_args();

    for (int i = 0; i < g_nsect; i++)
        for (int j = 0; j < g_sect[i].n; j++) { free(g_sect[i].key[j]); free(g_sect[i].val[j]); }
    free(trace);
    return vec_report("crypto_tls13_client_test");
}
