/*
 * nxtls - TLS 1.3 client handshake and connection (RFC 8446).
 *
 * One connection is one conn_t, laid over the caller's nxt_tls_conn
 * memory. The handshake is a straight sequence (ClientHello, ServerHello
 * or HelloRetryRequest, EncryptedExtensions, optional
 * CertificateRequest, Certificate, CertificateVerify, Finished, then the
 * client's Finished); every message that arrives out of that order is an
 * unexpected_message. Every error path sends the alert RFC 8446 asks for
 * (when there is one), wipes all secrets and leaves the connection
 * unusable.
 *
 * Checked by tests/host/crypto_tls13_client_test.c (RFC 8448 replay and
 * a scripted server that breaks each rule in turn).
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include "tls13.h"
#include "names.h"
#include "nxtls/tls.h"

#define CONN_MAGIC 0x4e58544cu          /* "NXTL" */

enum { ST_IDLE = 0, ST_HANDSHAKE, ST_OPEN, ST_PEER_CLOSED, ST_CLOSED, ST_FAILED };

/* Extension types (RFC 8446 4.2 and others). */
#define EXT_SERVER_NAME         0u
#define EXT_SUPPORTED_GROUPS   10u
#define EXT_SIG_ALGS           13u
#define EXT_ALPN               16u
#define EXT_RECORD_SIZE_LIMIT  28u
#define EXT_PRE_SHARED_KEY     41u
#define EXT_SUPPORTED_VERSIONS 43u
#define EXT_COOKIE             44u
#define EXT_KEY_SHARE          51u

/* Handshake message types. */
#define HS_CLIENT_HELLO          1u
#define HS_SERVER_HELLO          2u
#define HS_NEW_SESSION_TICKET    4u
#define HS_ENCRYPTED_EXTENSIONS  8u
#define HS_CERTIFICATE          11u
#define HS_CERTIFICATE_REQUEST  13u
#define HS_CERTIFICATE_VERIFY   15u
#define HS_FINISHED             20u
#define HS_KEY_UPDATE           24u
#define HS_MESSAGE_HASH        254u

#define GROUP_X25519 0x001du
#define TLS13        0x0304u
#define MAX_OFFERED  32
#define MAX_EXTS     32

/* Signature schemes (RFC 8446 4.2.3). */
#define SIG_ECDSA_P256_SHA256 0x0403u
#define SIG_ECDSA_P384_SHA384 0x0503u
#define SIG_RSA_PSS_SHA256    0x0804u
#define SIG_RSA_PSS_SHA384    0x0805u
#define SIG_RSA_PSS_SHA512    0x0806u
#define SIG_ED25519           0x0807u
#define SIG_RSA_PKCS1_SHA256  0x0401u
#define SIG_RSA_PKCS1_SHA384  0x0501u
#define SIG_RSA_PKCS1_SHA512  0x0601u

/* Offered in this order. The PKCS#1 schemes are for certificate
 * signatures only (RFC 8446 4.2.3); a CertificateVerify that uses one is
 * refused. */
static const uint16_t SIG_SCHEMES[] = {
    SIG_ECDSA_P256_SHA256, SIG_ECDSA_P384_SHA384, SIG_ED25519,
    SIG_RSA_PSS_SHA256, SIG_RSA_PSS_SHA384, SIG_RSA_PSS_SHA512,
    SIG_RSA_PKCS1_SHA256, SIG_RSA_PKCS1_SHA384, SIG_RSA_PKCS1_SHA512,
};

/* ChaCha20 first: nxcrypto's constant-time AES is much slower. */
static const uint16_t SUITES[] = {
    NXT_TLS_CHACHA20_POLY1305_SHA256, NXT_TLS_AES_128_GCM_SHA256, NXT_TLS_AES_256_GCM_SHA384,
};

/* SHA-256("HelloRetryRequest"), the ServerHello.random of a HelloRetryRequest. */
static const uint8_t HRR_RANDOM[32] = {
    0xcf, 0x21, 0xad, 0x74, 0xe5, 0x9a, 0x61, 0x11, 0xbe, 0x1d, 0x8c, 0x02, 0x1e, 0x65, 0xb8, 0x91,
    0xc2, 0xa2, 0x11, 0x16, 0x7a, 0xbb, 0x8c, 0x5e, 0x07, 0x9e, 0x09, 0xe2, 0xc8, 0xa8, 0x33, 0x9c,
};

static const char CV_CONTEXT[] = "TLS 1.3, server CertificateVerify";

typedef struct {
    uint32_t magic;
    int state;
    nxt_tls_info info;

    /* Configuration, copied by nxt_tls_connect. */
    char host[NXT_TLS_MAX_HOST + 1];
    size_t host_len;
    int host_is_ip;
    char alpn[NXT_TLS_MAX_ALPN + 1];
    size_t alpn_len;
    const nxt_trust_store *roots;
    int64_t now;
    nxt_tls_io io;

    /* Handshake state. */
    const nxt__suite *suite;
    nxc_hash_ctx th256, th384;          /* transcript; both until the suite is known */
    nxt__keysched ks;
    uint8_t priv[32];                   /* X25519 private key */
    uint8_t client_random[32];
    uint8_t sid[32];
    uint8_t sid_len;
    uint16_t offered[MAX_OFFERED];      /* extension types sent in the ClientHello */
    uint8_t n_offered;
    uint16_t hrr_suite;                 /* suite of the HelloRetryRequest, 0 if none */
    uint8_t server_finished;            /* change_cipher_spec no longer allowed */
    uint8_t cr_context[255];
    uint8_t cr_context_len;
    uint16_t peer_limit;                /* largest content per record the server accepts */
    nxt_key_type_t peer_key_type;       /* the server's key, from its certificate */
    uint8_t peer_key[512];
    uint16_t peer_key_len;
    uint8_t peer_e[8];
    uint8_t peer_e_len;

    /* Records. */
    nxt__rprot rx, tx;
    uint8_t rx_secret[NXT__MAX_HASH], tx_secret[NXT__MAX_HASH];
    size_t in_len, in_used;             /* bytes in in[], bytes of the current record */
    size_t app_off, app_len;            /* decrypted application data not yet read */
    size_t hs_len, hs_used;             /* bytes in hs[], bytes of the current message */
    uint8_t in[NXT_TLS_MAX_RECORD];
    uint8_t out[NXT_TLS_MAX_RECORD];
    uint8_t hs[NXT_TLS_MAX_HANDSHAKE + NXT_TLS_MAX_PLAINTEXT];
    nxt_x509_cert certs[NXT_TLS_MAX_CERTS];
    uint8_t cert_index[NXT_TLS_MAX_CERTS];

#ifdef NXT_TESTING
    struct {
        const uint8_t *hello;
        size_t hello_len;
        uint8_t priv[32];
        int key_set;
        nxt_key_type_t key_type;
        uint8_t key[512];
        uint16_t key_len;
        uint8_t e[8];
        uint8_t e_len;
    } test;
#endif
} conn_t;

_Static_assert(sizeof(conn_t) <= NXT_TLS_CONN_SIZE, "NXT_TLS_CONN_SIZE is too small");

static conn_t *C(nxt_tls_conn *conn) { return (conn_t *)(void *)conn->priv.bytes; }

/* ------------------------------------------------------------------ */
/* Byte helpers                                                         */
/* ------------------------------------------------------------------ */

static void copy(uint8_t *dst, const uint8_t *src, size_t n) {
    for (size_t i = 0; i < n; i++) dst[i] = src[i];
}

/* Move n bytes down to dst (dst < src). */
static void move_down(uint8_t *dst, const uint8_t *src, size_t n) {
    for (size_t i = 0; i < n; i++) dst[i] = src[i];
}

static int bytes_equal(const uint8_t *a, const uint8_t *b, size_t n) {
    for (size_t i = 0; i < n; i++)
        if (a[i] != b[i]) return 0;
    return 1;
}

/* Reader over a received message. Every read checks the remaining length. */
typedef struct { const uint8_t *p; size_t n; } rd;

static int rd_u8(rd *r, uint8_t *v) {
    if (r->n < 1) return 0;
    *v = r->p[0];
    r->p++; r->n--;
    return 1;
}

static int rd_u16(rd *r, uint16_t *v) {
    if (r->n < 2) return 0;
    *v = (uint16_t)(r->p[0] << 8 | r->p[1]);
    r->p += 2; r->n -= 2;
    return 1;
}

static int rd_bytes(rd *r, size_t n, const uint8_t **out) {
    if (r->n < n) return 0;
    *out = r->p;
    r->p += n; r->n -= n;
    return 1;
}

/* A vector with a 1, 2 or 3 byte length prefix. */
static int rd_vec(rd *r, int lenbytes, rd *sub) {
    if (r->n < (size_t)lenbytes) return 0;
    size_t n = 0;
    for (int i = 0; i < lenbytes; i++) n = n << 8 | r->p[i];
    if (r->n - (size_t)lenbytes < n) return 0;
    sub->p = r->p + lenbytes;
    sub->n = n;
    r->p += (size_t)lenbytes + n;
    r->n -= (size_t)lenbytes + n;
    return 1;
}

/* Writer into a fixed buffer; any overflow is remembered and reported once. */
typedef struct { uint8_t *p; size_t n, cap; int err; } wr;

static void w_u8(wr *w, unsigned v) {
    if (w->n + 1 > w->cap) { w->err = 1; return; }
    w->p[w->n++] = (uint8_t)v;
}

static void w_u16(wr *w, unsigned v) { w_u8(w, v >> 8); w_u8(w, v); }

static void w_bytes(wr *w, const uint8_t *b, size_t n) {
    if (w->n + n > w->cap) { w->err = 1; return; }
    copy(w->p + w->n, b, n);
    w->n += n;
}

/* Open a length prefix of lenbytes; close it with w_close. */
static size_t w_open(wr *w, int lenbytes) {
    size_t at = w->n;
    for (int i = 0; i < lenbytes; i++) w_u8(w, 0);
    return at;
}

static void w_close(wr *w, size_t at, int lenbytes) {
    if (w->err) return;
    size_t n = w->n - at - (size_t)lenbytes;
    if (lenbytes < 3 && n >> (8 * lenbytes)) { w->err = 1; return; }
    for (int i = 0; i < lenbytes; i++)
        w->p[at + (size_t)i] = (uint8_t)(n >> (8 * (lenbytes - 1 - i)));
}

/* ------------------------------------------------------------------ */
/* Errors and alerts                                                    */
/* ------------------------------------------------------------------ */

static int cert_alert(int x509_err) {
    switch (x509_err) {
    case NXT_X509_ERR_EXPIRED:
    case NXT_X509_ERR_NOT_YET_VALID:
        return NXT_TLS_ALERT_CERTIFICATE_EXPIRED;
    case NXT_X509_ERR_UNKNOWN_ISSUER:
        return NXT_TLS_ALERT_UNKNOWN_CA;
    case NXT_X509_ERR_ENCODING: case NXT_X509_ERR_VERSION: case NXT_X509_ERR_TIME:
    case NXT_X509_ERR_KEY: case NXT_X509_ERR_WEAK_KEY: case NXT_X509_ERR_SIG_ALG:
    case NXT_X509_ERR_CRITICAL_EXT: case NXT_X509_ERR_DUPLICATE_EXT:
    case NXT_X509_ERR_EXTENSION: case NXT_X509_ERR_BAD_SIGNATURE:
        return NXT_TLS_ALERT_BAD_CERTIFICATE;
    default:
        return NXT_TLS_ALERT_CERTIFICATE_UNKNOWN;
    }
}

/* The alert RFC 8446 6.2 asks for, or -1 when none is sent (the peer is
 * gone, already sent an alert, or nothing was sent yet). */
static int alert_for(const conn_t *c, int err) {
    switch (err) {
    case NXT_TLS_ERR_UNEXPECTED_MESSAGE: return NXT_TLS_ALERT_UNEXPECTED_MESSAGE;
    case NXT_TLS_ERR_BAD_RECORD_MAC: return NXT_TLS_ALERT_BAD_RECORD_MAC;
    case NXT_TLS_ERR_RECORD_OVERFLOW: return NXT_TLS_ALERT_RECORD_OVERFLOW;
    case NXT_TLS_ERR_HANDSHAKE_FAILURE: return NXT_TLS_ALERT_HANDSHAKE_FAILURE;
    case NXT_TLS_ERR_CERTIFICATE: return cert_alert(c->info.cert_error);
    case NXT_TLS_ERR_ILLEGAL_PARAMETER: return NXT_TLS_ALERT_ILLEGAL_PARAMETER;
    case NXT_TLS_ERR_DECODE: return NXT_TLS_ALERT_DECODE_ERROR;
    case NXT_TLS_ERR_BAD_SIGNATURE:
    case NXT_TLS_ERR_BAD_FINISHED: return NXT_TLS_ALERT_DECRYPT_ERROR;
    case NXT_TLS_ERR_PROTOCOL_VERSION: return NXT_TLS_ALERT_PROTOCOL_VERSION;
    case NXT_TLS_ERR_UNSUPPORTED_EXTENSION: return NXT_TLS_ALERT_UNSUPPORTED_EXTENSION;
    case NXT_TLS_ERR_MISSING_EXTENSION: return NXT_TLS_ALERT_MISSING_EXTENSION;
    case NXT_TLS_ERR_TOO_LARGE:
    case NXT_TLS_ERR_CRYPTO:
    case NXT_TLS_ERR_BUFFER: return NXT_TLS_ALERT_INTERNAL_ERROR;
    default: return -1;        /* IO, CLOSED, ALERT, RANDOM, ARG, SEQ_EXHAUSTED */
    }
}

static int send_all(conn_t *c, const uint8_t *p, size_t n) {
    while (n > 0) {
        long r = c->io.send(c->io.ctx, p, n);
        if (r <= 0 || (unsigned long)r > n) return NXT_TLS_ERR_IO;
        p += r;
        n -= (size_t)r;
    }
    return NXT_TLS_OK;
}

/* Send a protected record of `type` built from data (may be in out + 5). */
static int send_sealed(conn_t *c, uint8_t type, const uint8_t *data, size_t len) {
    size_t out_len = 0;
    int rc = nxt__rprot_seal(&c->tx, type, data, len, 0, c->out, sizeof(c->out), &out_len);
    if (rc != NXT_TLS_OK) return rc;
    return send_all(c, c->out, out_len);
}

/* Send a fatal alert (level 2). close_notify is sent by nxt_tls_close(). */
static void send_fatal_alert(conn_t *c, uint8_t desc) {
    uint8_t a[2] = { 2, desc };
    if (c->tx.ready) {
        (void)send_sealed(c, NXT_TLS_CT_ALERT, a, 2);
    } else {
        uint8_t rec[7] = { NXT_TLS_CT_ALERT, 3, 3, 0, 2, a[0], a[1] };
        (void)send_all(c, rec, sizeof(rec));
    }
}

static void wipe_secrets(conn_t *c) {
    nxt__ks_wipe(&c->ks);
    nxt__rprot_wipe(&c->rx);
    nxt__rprot_wipe(&c->tx);
    nxc_memzero(c->priv, sizeof(c->priv));
    nxc_memzero(c->rx_secret, sizeof(c->rx_secret));
    nxc_memzero(c->tx_secret, sizeof(c->tx_secret));
    nxc_memzero(&c->th256, sizeof(c->th256));
    nxc_memzero(&c->th384, sizeof(c->th384));
    nxc_memzero(c->in, sizeof(c->in));
    nxc_memzero(c->out, sizeof(c->out));
    nxc_memzero(c->hs, sizeof(c->hs));
    c->in_len = c->in_used = c->app_off = c->app_len = c->hs_len = c->hs_used = 0;
}

/* Fail the connection: send the alert for err (if any), wipe, and return err. */
static int fail(conn_t *c, int err) {
    if (c->state == ST_FAILED) return err;
    int a = alert_for(c, err);
    if (a >= 0 && c->state != ST_IDLE) {
        send_fatal_alert(c, (uint8_t)a);
        c->info.alert_sent = a;
    }
    wipe_secrets(c);
    c->state = ST_FAILED;
    return err;
}

/* ------------------------------------------------------------------ */
/* Transcript                                                           */
/* ------------------------------------------------------------------ */

static nxc_hash_ctx *th_ctx(conn_t *c) {
    return c->suite->hash == NXC_HASH_SHA384 ? &c->th384 : &c->th256;
}

static int th_update(conn_t *c, const uint8_t *m, size_t n) {
    int rc;
    if (c->suite == NULL)
        rc = (nxc_hash_update(&c->th256, m, n) == NXC_OK &&
              nxc_hash_update(&c->th384, m, n) == NXC_OK) ? NXC_OK : NXC_ERR_ARG;
    else
        rc = nxc_hash_update(th_ctx(c), m, n);
    return rc == NXC_OK ? NXT_TLS_OK : NXT_TLS_ERR_CRYPTO;
}

/* Transcript-Hash of the messages so far (the running hash is kept). */
static int th_get(conn_t *c, uint8_t out[NXT__MAX_HASH]) {
    nxc_hash_ctx copy_ctx = *th_ctx(c);
    int rc = nxc_hash_final(&copy_ctx, out, NXT__MAX_HASH);
    nxc_memzero(&copy_ctx, sizeof(copy_ctx));
    return rc == NXC_OK ? NXT_TLS_OK : NXT_TLS_ERR_CRYPTO;
}

/* ------------------------------------------------------------------ */
/* Reading records and handshake messages                               */
/* ------------------------------------------------------------------ */

/* Make in[] start with one whole record; *rec_len is its length. */
static int read_record(conn_t *c, size_t *rec_len) {
    if (c->in_used > 0) {
        move_down(c->in, c->in + c->in_used, c->in_len - c->in_used);
        c->in_len -= c->in_used;
        c->in_used = 0;
    }
    for (;;) {
        if (c->in_len >= NXT_TLS_RECORD_HEADER) {
            size_t body = (size_t)c->in[3] << 8 | c->in[4];
            size_t max = c->rx.ready ? NXT_TLS_MAX_CIPHERTEXT : NXT_TLS_MAX_PLAINTEXT;
            if (body > max) return NXT_TLS_ERR_RECORD_OVERFLOW;
            if (c->in_len >= NXT_TLS_RECORD_HEADER + body) {
                *rec_len = NXT_TLS_RECORD_HEADER + body;
                c->in_used = *rec_len;
                return NXT_TLS_OK;
            }
        }
        size_t space = sizeof(c->in) - c->in_len;
        long r = c->io.recv(c->io.ctx, c->in + c->in_len, space);
        if (r == 0) return NXT_TLS_ERR_CLOSED;
        if (r < 0 || (unsigned long)r > space) return NXT_TLS_ERR_IO;
        c->in_len += (size_t)r;
    }
}

/* The next record's real content type and plaintext (in place in in[]).
 * change_cipher_spec records are dropped where RFC 8446 5 allows them. */
static int next_content(conn_t *c, uint8_t *type, uint8_t **data, size_t *len) {
    for (;;) {
        size_t rl = 0;
        int rc = read_record(c, &rl);
        if (rc != NXT_TLS_OK) return rc;
        uint8_t t = c->in[0];
        if (t == NXT_TLS_CT_CHANGE_CIPHER_SPEC) {
            if (c->state == ST_HANDSHAKE && !c->server_finished &&
                rl == NXT_TLS_RECORD_HEADER + 1 && c->in[5] == 1)
                continue;
            return NXT_TLS_ERR_UNEXPECTED_MESSAGE;
        }
        size_t plen;
        if (c->rx.ready) {
            rc = nxt__rprot_open(&c->rx, c->in, rl, &t, &plen);
            if (rc != NXT_TLS_OK) return rc;
        } else {
            if (t != NXT_TLS_CT_HANDSHAKE && t != NXT_TLS_CT_ALERT)
                return NXT_TLS_ERR_UNEXPECTED_MESSAGE;
            plen = rl - NXT_TLS_RECORD_HEADER;
        }
        if (t != NXT_TLS_CT_HANDSHAKE && t != NXT_TLS_CT_ALERT && t != NXT_TLS_CT_APPLICATION_DATA)
            return NXT_TLS_ERR_UNEXPECTED_MESSAGE;
        /* Zero-length handshake or alert fragments are not allowed (RFC 8446 5.1). */
        if (plen == 0 && t != NXT_TLS_CT_APPLICATION_DATA) return NXT_TLS_ERR_UNEXPECTED_MESSAGE;
        *type = t;
        *data = c->in + NXT_TLS_RECORD_HEADER;
        *len = plen;
        return NXT_TLS_OK;
    }
}

/* An alert from the server: 1 for close_notify, 0 to ignore
 * (user_canceled, which close_notify follows), or an error. */
static int on_alert(conn_t *c, const uint8_t *d, size_t n) {
    if (n != 2) return NXT_TLS_ERR_DECODE;
    c->info.alert_received = d[1];
    if (d[1] == NXT_TLS_ALERT_CLOSE_NOTIFY) return 1;
    if (d[1] == NXT_TLS_ALERT_USER_CANCELED) return 0;
    return NXT_TLS_ERR_ALERT;
}

/* Bytes of a later handshake message already buffered. A key change must
 * not happen in the middle of a record's handshake data (RFC 8446 5.1). */
static size_t hs_pending(const conn_t *c) {
    return c->hs_len - c->hs_used;
}

/* The next complete handshake message: *msg points at its 4-byte header. */
static int next_hs(conn_t *c, const uint8_t **msg, size_t *len) {
    if (c->hs_used > 0) {
        move_down(c->hs, c->hs + c->hs_used, c->hs_len - c->hs_used);
        c->hs_len -= c->hs_used;
        c->hs_used = 0;
    }
    for (;;) {
        if (c->hs_len >= 4) {
            size_t body = (size_t)c->hs[1] << 16 | (size_t)c->hs[2] << 8 | c->hs[3];
            if (body > NXT_TLS_MAX_HANDSHAKE - 4) return NXT_TLS_ERR_TOO_LARGE;
            if (c->hs_len >= 4 + body) {
                *msg = c->hs;
                *len = 4 + body;
                c->hs_used = 4 + body;
                return NXT_TLS_OK;
            }
        }
        uint8_t type;
        uint8_t *d;
        size_t n;
        int rc = next_content(c, &type, &d, &n);
        if (rc != NXT_TLS_OK) return rc;
        if (type == NXT_TLS_CT_ALERT) {
            rc = on_alert(c, d, n);
            if (rc == 0) continue;
            /* close_notify in the middle of a handshake is a failure too. */
            return rc == 1 ? NXT_TLS_ERR_ALERT : rc;
        }
        if (type != NXT_TLS_CT_HANDSHAKE) return NXT_TLS_ERR_UNEXPECTED_MESSAGE;
        if (n > sizeof(c->hs) - c->hs_len) return NXT_TLS_ERR_TOO_LARGE;
        copy(c->hs + c->hs_len, d, n);
        c->hs_len += n;
    }
}

/* ------------------------------------------------------------------ */
/* Extensions                                                           */
/* ------------------------------------------------------------------ */

typedef struct { uint16_t type; rd body; } ext_t;

static int offered(const conn_t *c, uint16_t t) {
    for (size_t i = 0; i < c->n_offered; i++)
        if (c->offered[i] == t) return 1;
    return 0;
}

static void note_offered(conn_t *c, uint16_t t) {
    if (!offered(c, t) && c->n_offered < MAX_OFFERED) c->offered[c->n_offered++] = t;
}

/* Parse an extension block (the whole rest of r). Duplicates are refused. */
static int parse_exts(rd *r, ext_t *out, size_t *count) {
    rd all;
    if (!rd_vec(r, 2, &all) || r->n != 0) return NXT_TLS_ERR_DECODE;
    size_t n = 0;
    while (all.n > 0) {
        uint16_t t;
        rd body;
        if (!rd_u16(&all, &t) || !rd_vec(&all, 2, &body)) return NXT_TLS_ERR_DECODE;
        for (size_t i = 0; i < n; i++)
            if (out[i].type == t) return NXT_TLS_ERR_ILLEGAL_PARAMETER;
        if (n == MAX_EXTS) return NXT_TLS_ERR_DECODE;
        out[n].type = t;
        out[n].body = body;
        n++;
    }
    *count = n;
    return NXT_TLS_OK;
}

/* RFC 8446 4.2: an extension the client did not offer is
 * unsupported_extension; one it offered that does not belong in this
 * message is illegal_parameter. */
static int check_exts(const conn_t *c, const ext_t *e, size_t n, const uint16_t *allowed,
                      size_t n_allowed, int cookie_ok) {
    for (size_t i = 0; i < n; i++) {
        int ok = 0;
        for (size_t j = 0; j < n_allowed; j++)
            if (allowed[j] == e[i].type) ok = 1;
        int was_offered = offered(c, e[i].type) || (cookie_ok && e[i].type == EXT_COOKIE);
        if (!was_offered) return NXT_TLS_ERR_UNSUPPORTED_EXTENSION;
        if (!ok) return NXT_TLS_ERR_ILLEGAL_PARAMETER;
    }
    return NXT_TLS_OK;
}

static const ext_t *find_ext(const ext_t *e, size_t n, uint16_t t) {
    for (size_t i = 0; i < n; i++)
        if (e[i].type == t) return &e[i];
    return NULL;
}

/* ------------------------------------------------------------------ */
/* ClientHello                                                          */
/* ------------------------------------------------------------------ */

/* Build the ClientHello (the message, without record header) at w. */
static void build_client_hello(conn_t *c, wr *w, const uint8_t *cookie, size_t cookie_len) {
    uint8_t pub[32];
    int key_ok = nxc_x25519_public(pub, c->priv) == NXC_OK;
    if (!key_ok) w->err = 1;

    w_u8(w, HS_CLIENT_HELLO);
    size_t msg = w_open(w, 3);
    w_u16(w, 0x0303);                                    /* legacy_version */
    w_bytes(w, c->client_random, 32);
    w_u8(w, c->sid_len);
    w_bytes(w, c->sid, c->sid_len);
    size_t at = w_open(w, 2);
    for (size_t i = 0; i < sizeof(SUITES) / sizeof(SUITES[0]); i++) w_u16(w, SUITES[i]);
    w_close(w, at, 2);
    w_u8(w, 1);                                          /* legacy_compression_methods */
    w_u8(w, 0);

    size_t exts = w_open(w, 2);
    if (!c->host_is_ip) {                                /* RFC 6066: no IP literals in SNI */
        w_u16(w, EXT_SERVER_NAME);
        size_t e = w_open(w, 2), list = w_open(w, 2);
        w_u8(w, 0);                                      /* host_name */
        size_t name = w_open(w, 2);
        w_bytes(w, (const uint8_t *)c->host, c->host_len);
        w_close(w, name, 2);
        w_close(w, list, 2);
        w_close(w, e, 2);
        note_offered(c, EXT_SERVER_NAME);
    }
    w_u16(w, EXT_SUPPORTED_VERSIONS);
    at = w_open(w, 2);
    w_u8(w, 2);
    w_u16(w, TLS13);
    w_close(w, at, 2);
    note_offered(c, EXT_SUPPORTED_VERSIONS);

    w_u16(w, EXT_SUPPORTED_GROUPS);
    at = w_open(w, 2);
    w_u16(w, 2);
    w_u16(w, GROUP_X25519);
    w_close(w, at, 2);
    note_offered(c, EXT_SUPPORTED_GROUPS);

    w_u16(w, EXT_SIG_ALGS);
    at = w_open(w, 2);
    size_t list = w_open(w, 2);
    for (size_t i = 0; i < sizeof(SIG_SCHEMES) / sizeof(SIG_SCHEMES[0]); i++)
        w_u16(w, SIG_SCHEMES[i]);
    w_close(w, list, 2);
    w_close(w, at, 2);
    note_offered(c, EXT_SIG_ALGS);

    w_u16(w, EXT_KEY_SHARE);
    at = w_open(w, 2);
    list = w_open(w, 2);
    w_u16(w, GROUP_X25519);
    w_u16(w, 32);
    w_bytes(w, pub, 32);
    w_close(w, list, 2);
    w_close(w, at, 2);
    note_offered(c, EXT_KEY_SHARE);

    if (c->alpn_len > 0) {
        w_u16(w, EXT_ALPN);
        at = w_open(w, 2);
        list = w_open(w, 2);
        w_u8(w, (unsigned)c->alpn_len);
        w_bytes(w, (const uint8_t *)c->alpn, c->alpn_len);
        w_close(w, list, 2);
        w_close(w, at, 2);
        note_offered(c, EXT_ALPN);
    }
    /* record_size_limit (RFC 8449): we accept full-size records; a
     * constrained server can answer with a smaller limit for our records. */
    w_u16(w, EXT_RECORD_SIZE_LIMIT);
    w_u16(w, 2);
    w_u16(w, NXT_TLS_MAX_PLAINTEXT + 1u);
    note_offered(c, EXT_RECORD_SIZE_LIMIT);

    if (cookie != NULL) {
        w_u16(w, EXT_COOKIE);
        at = w_open(w, 2);
        size_t ck = w_open(w, 2);
        w_bytes(w, cookie, cookie_len);
        w_close(w, ck, 2);
        w_close(w, at, 2);
        note_offered(c, EXT_COOKIE);
    }
    w_close(w, exts, 2);
    w_close(w, msg, 3);
    nxc_memzero(pub, sizeof(pub));
}

#ifdef NXT_TESTING
/* Test hook: take random, session id and offered extensions from a given
 * ClientHello (the RFC 8448 one) instead of building our own. */
static int adopt_test_hello(conn_t *c) {
    rd r = { c->test.hello, c->test.hello_len };
    uint8_t t;
    uint32_t len = 0;
    const uint8_t *p;
    rd v, exts;
    if (!rd_u8(&r, &t) || t != HS_CLIENT_HELLO || !rd_bytes(&r, 3, &p)) return NXT_TLS_ERR_ARG;
    len = (uint32_t)p[0] << 16 | (uint32_t)p[1] << 8 | p[2];
    if (len != r.n || !rd_bytes(&r, 2, &p) || !rd_bytes(&r, 32, &p)) return NXT_TLS_ERR_ARG;
    copy(c->client_random, p, 32);
    if (!rd_vec(&r, 1, &v) || v.n > 32) return NXT_TLS_ERR_ARG;
    copy(c->sid, v.p, v.n);
    c->sid_len = (uint8_t)v.n;
    if (!rd_vec(&r, 2, &v) || !rd_vec(&r, 1, &v) || !rd_vec(&r, 2, &exts) || r.n != 0)
        return NXT_TLS_ERR_ARG;
    while (exts.n > 0) {
        uint16_t et;
        if (!rd_u16(&exts, &et) || !rd_vec(&exts, 2, &v)) return NXT_TLS_ERR_ARG;
        note_offered(c, et);
    }
    copy(c->priv, c->test.priv, 32);
    return NXT_TLS_OK;
}
#endif

/* Send a ClientHello message as a plaintext record and add it to the transcript. */
static int send_hello_record(conn_t *c, const uint8_t *msg, size_t len, int first) {
    if (len > NXT_TLS_MAX_PLAINTEXT) return NXT_TLS_ERR_BUFFER;
    /* legacy_record_version: 0x0301 for the first ClientHello, then 0x0303 (RFC 8446 5.1). */
    uint8_t hdr[5] = { NXT_TLS_CT_HANDSHAKE, 3, first ? 1 : 3, (uint8_t)(len >> 8), (uint8_t)len };
    int rc = th_update(c, msg, len);
    if (rc == NXT_TLS_OK) rc = send_all(c, hdr, 5);
    if (rc == NXT_TLS_OK) rc = send_all(c, msg, len);
    return rc;
}

/* ------------------------------------------------------------------ */
/* ServerHello and HelloRetryRequest                                    */
/* ------------------------------------------------------------------ */

static int suite_offered(const conn_t *c, uint16_t id) {
#ifdef NXT_TESTING
    if (c->test.hello) return nxt__suite_find(id) != NULL;
#else
    (void)c;
#endif
    for (size_t i = 0; i < sizeof(SUITES) / sizeof(SUITES[0]); i++)
        if (SUITES[i] == id) return 1;
    return 0;
}

/* After a HelloRetryRequest: replace ClientHello1 in the transcript with
 * message_hash(Hash(ClientHello1)), add the HRR, and send ClientHello2
 * with the cookie (RFC 8446 4.1.4, 4.4.1). */
static int retry_hello(conn_t *c, const uint8_t *hrr, size_t hrr_len, const ext_t *cookie) {
    uint8_t h[NXT__MAX_HASH];
    int rc = th_get(c, h);
    if (rc != NXT_TLS_OK) return rc;
    uint8_t mh[4] = { HS_MESSAGE_HASH, 0, 0, c->suite->hash_len };
    if (nxc_hash_init(th_ctx(c), c->suite->hash) != NXC_OK) return NXT_TLS_ERR_CRYPTO;
    rc = th_update(c, mh, 4);
    if (rc == NXT_TLS_OK) rc = th_update(c, h, c->suite->hash_len);
    if (rc == NXT_TLS_OK) rc = th_update(c, hrr, hrr_len);
    if (rc != NXT_TLS_OK) return rc;
#ifdef NXT_TESTING
    if (c->test.hello) return NXT_TLS_ERR_ARG;     /* the trace has no second hello to replay */
#endif
    wr w = { c->out, 0, sizeof(c->out), 0 };
    build_client_hello(c, &w, cookie ? cookie->body.p : NULL, cookie ? cookie->body.n : 0);
    if (w.err) return NXT_TLS_ERR_BUFFER;
    return send_hello_record(c, c->out, w.n, 0);
}

/* Process ServerHello (or HelloRetryRequest). *retried is set when it was
 * an HRR and ClientHello2 has been sent. */
static int on_server_hello(conn_t *c, const uint8_t *m, size_t n, int *retried) {
    rd r = { m + 4, n - 4 };
    uint16_t version, suite_id;
    const uint8_t *random;
    rd sid;
    uint8_t comp;
    *retried = 0;
    if (!rd_u16(&r, &version) || !rd_bytes(&r, 32, &random) || !rd_vec(&r, 1, &sid) ||
        !rd_u16(&r, &suite_id) || !rd_u8(&r, &comp))
        return NXT_TLS_ERR_DECODE;
    if (version < 0x0303) return NXT_TLS_ERR_PROTOCOL_VERSION;
    /* A TLS 1.2 server may send no extensions at all. */
    if (r.n == 0) return NXT_TLS_ERR_PROTOCOL_VERSION;
    ext_t e[MAX_EXTS];
    size_t ne = 0;
    int rc = parse_exts(&r, e, &ne);
    if (rc != NXT_TLS_OK) return rc;

    /* Version first: without supported_versions this is TLS 1.2 or older. */
    const ext_t *sv = find_ext(e, ne, EXT_SUPPORTED_VERSIONS);
    if (sv == NULL) return NXT_TLS_ERR_PROTOCOL_VERSION;
    rd svr = sv->body;
    uint16_t selected;
    if (!rd_u16(&svr, &selected) || svr.n != 0) return NXT_TLS_ERR_DECODE;
    if (selected != TLS13 || version != 0x0303) return NXT_TLS_ERR_ILLEGAL_PARAMETER;

    if (sid.n != c->sid_len || !bytes_equal(sid.p, c->sid, sid.n))
        return NXT_TLS_ERR_ILLEGAL_PARAMETER;
    if (!suite_offered(c, suite_id) || comp != 0) return NXT_TLS_ERR_ILLEGAL_PARAMETER;
    if (c->hrr_suite != 0 && suite_id != c->hrr_suite) return NXT_TLS_ERR_ILLEGAL_PARAMETER;

    int is_hrr = bytes_equal(random, HRR_RANDOM, 32);
    if (is_hrr) {
        static const uint16_t allowed[] = { EXT_SUPPORTED_VERSIONS, EXT_KEY_SHARE, EXT_COOKIE };
        if (c->hrr_suite != 0) return NXT_TLS_ERR_UNEXPECTED_MESSAGE;   /* second HRR */
        rc = check_exts(c, e, ne, allowed, 3, 1);
        if (rc != NXT_TLS_OK) return rc;
        const ext_t *ks = find_ext(e, ne, EXT_KEY_SHARE);
        const ext_t *cookie = find_ext(e, ne, EXT_COOKIE);
        if (ks != NULL) {
            rd kr = ks->body;
            uint16_t group;
            if (!rd_u16(&kr, &group) || kr.n != 0) return NXT_TLS_ERR_DECODE;
            /* The only group offered is X25519, and a share for it was sent:
             * asking for it, or for any other group, is not allowed. */
            return NXT_TLS_ERR_ILLEGAL_PARAMETER;
        }
        if (cookie == NULL) return NXT_TLS_ERR_ILLEGAL_PARAMETER;      /* would change nothing */
        if (cookie->body.n < 3) return NXT_TLS_ERR_DECODE;
        rd ck = cookie->body, ckv;
        if (!rd_vec(&ck, 2, &ckv) || ck.n != 0 || ckv.n == 0) return NXT_TLS_ERR_DECODE;
        c->suite = nxt__suite_find(suite_id);
        c->hrr_suite = suite_id;
        c->info.hello_retry = 1;
        c->info.suite = suite_id;
        ext_t ce = { EXT_COOKIE, ckv };
        rc = retry_hello(c, m, n, &ce);
        if (rc != NXT_TLS_OK) return rc;
        *retried = 1;
        return NXT_TLS_OK;
    }

    static const uint16_t allowed[] = { EXT_SUPPORTED_VERSIONS, EXT_KEY_SHARE };
    rc = check_exts(c, e, ne, allowed, 2, 0);
    if (rc != NXT_TLS_OK) return rc;
    const ext_t *ks = find_ext(e, ne, EXT_KEY_SHARE);
    if (ks == NULL) return NXT_TLS_ERR_MISSING_EXTENSION;
    rd kr = ks->body, key;
    uint16_t group;
    if (!rd_u16(&kr, &group) || !rd_vec(&kr, 2, &key) || kr.n != 0) return NXT_TLS_ERR_DECODE;
    if (group != GROUP_X25519 || key.n != 32) return NXT_TLS_ERR_ILLEGAL_PARAMETER;

    c->suite = nxt__suite_find(suite_id);
    c->info.suite = suite_id;
    rc = th_update(c, m, n);
    if (rc != NXT_TLS_OK) return rc;

    uint8_t shared[32], th[NXT__MAX_HASH];
    int xrc = nxc_x25519(shared, c->priv, key.p);
    nxc_memzero(c->priv, sizeof(c->priv));
    if (xrc == NXC_ERR_WEAK) {
        nxc_memzero(shared, sizeof(shared));
        return NXT_TLS_ERR_ILLEGAL_PARAMETER;        /* low-order point */
    }
    if (xrc != NXC_OK) return NXT_TLS_ERR_CRYPTO;
    rc = th_get(c, th);
    if (rc == NXT_TLS_OK) rc = nxt__ks_init(&c->ks, c->suite);
    if (rc == NXT_TLS_OK) rc = nxt__ks_handshake(&c->ks, shared, sizeof(shared), th);
    nxc_memzero(shared, sizeof(shared));
    if (rc == NXT_TLS_OK) rc = nxt__rprot_init(&c->rx, c->suite, c->ks.server_hs);
    if (rc == NXT_TLS_OK) rc = nxt__rprot_init(&c->tx, c->suite, c->ks.client_hs);
    return rc;
}

/* ------------------------------------------------------------------ */
/* The server's encrypted flight                                        */
/* ------------------------------------------------------------------ */

static int on_encrypted_extensions(conn_t *c, const uint8_t *m, size_t n) {
    static const uint16_t allowed[] = { EXT_SERVER_NAME, EXT_SUPPORTED_GROUPS, EXT_ALPN,
                                        EXT_RECORD_SIZE_LIMIT };
    rd r = { m + 4, n - 4 };
    ext_t e[MAX_EXTS];
    size_t ne = 0;
    int rc = parse_exts(&r, e, &ne);
    if (rc == NXT_TLS_OK) rc = check_exts(c, e, ne, allowed, 4, 0);
    if (rc != NXT_TLS_OK) return rc;
    for (size_t i = 0; i < ne; i++) {
        rd b = e[i].body;
        if (e[i].type == EXT_SERVER_NAME) {
            if (b.n != 0) return NXT_TLS_ERR_DECODE;
        } else if (e[i].type == EXT_SUPPORTED_GROUPS) {
            rd list;
            if (!rd_vec(&b, 2, &list) || b.n != 0 || list.n == 0 || list.n % 2)
                return NXT_TLS_ERR_DECODE;
        } else if (e[i].type == EXT_ALPN) {
            rd list, name;
            if (!rd_vec(&b, 2, &list) || b.n != 0 || !rd_vec(&list, 1, &name) || list.n != 0 ||
                name.n == 0)
                return NXT_TLS_ERR_DECODE;
            if (name.n != c->alpn_len || !bytes_equal(name.p, (const uint8_t *)c->alpn, name.n))
                return NXT_TLS_ERR_ILLEGAL_PARAMETER;
            copy((uint8_t *)c->info.alpn, name.p, name.n);
            c->info.alpn[name.n] = 0;
        } else if (e[i].type == EXT_RECORD_SIZE_LIMIT) {
            uint16_t limit;
            if (!rd_u16(&b, &limit) || b.n != 0) return NXT_TLS_ERR_DECODE;
            if (limit < 64) return NXT_TLS_ERR_ILLEGAL_PARAMETER;   /* RFC 8449 4 */
            /* The limit counts the inner content type byte. */
            size_t content = (size_t)limit - 1;
            c->peer_limit = (uint16_t)(content < NXT_TLS_MAX_PLAINTEXT ? content
                                                                       : NXT_TLS_MAX_PLAINTEXT);
        }
    }
    return NXT_TLS_OK;
}

static int on_certificate_request(conn_t *c, const uint8_t *m, size_t n) {
    rd r = { m + 4, n - 4 }, ctx;
    if (!rd_vec(&r, 1, &ctx)) return NXT_TLS_ERR_DECODE;
    ext_t e[MAX_EXTS];
    size_t ne = 0;
    int rc = parse_exts(&r, e, &ne);
    if (rc != NXT_TLS_OK) return rc;
    if (find_ext(e, ne, EXT_SIG_ALGS) == NULL) return NXT_TLS_ERR_MISSING_EXTENSION;
    copy(c->cr_context, ctx.p, ctx.n);
    c->cr_context_len = (uint8_t)ctx.n;
    c->info.cert_requested = 1;
    return NXT_TLS_OK;
}

static int keep_peer_key(conn_t *c, nxt_key_type_t type, const uint8_t *key, size_t key_len,
                         const uint8_t *e, size_t e_len) {
    if (key_len > sizeof(c->peer_key) || e_len > sizeof(c->peer_e)) return NXT_TLS_ERR_CERTIFICATE;
    c->peer_key_type = type;
    copy(c->peer_key, key, key_len);
    c->peer_key_len = (uint16_t)key_len;
    copy(c->peer_e, e, e_len);
    c->peer_e_len = (uint8_t)e_len;
    return NXT_TLS_OK;
}

static int cert_fail(conn_t *c, int x509_err, int index) {
    c->info.cert_error = x509_err;
    c->info.cert_failed_index = index;
    return NXT_TLS_ERR_CERTIFICATE;
}

static int on_certificate(conn_t *c, const uint8_t *m, size_t n) {
    rd r = { m + 4, n - 4 }, ctx, list;
    if (!rd_vec(&r, 1, &ctx) || !rd_vec(&r, 3, &list) || r.n != 0) return NXT_TLS_ERR_DECODE;
    if (ctx.n != 0) return NXT_TLS_ERR_ILLEGAL_PARAMETER;   /* server authentication */
    if (list.n == 0) return NXT_TLS_ERR_DECODE;             /* RFC 8446 4.4.2.4 */

    size_t total = 0, parsed = 0;
    int leaf_err = NXT_X509_OK;
    while (list.n > 0) {
        rd der, exts;
        if (!rd_vec(&list, 3, &der) || der.n == 0 || !rd_vec(&list, 2, &exts))
            return NXT_TLS_ERR_DECODE;
        /* Entry extensions (OCSP, SCT) are answers to requests nxtls never makes. */
        if (exts.n != 0) return NXT_TLS_ERR_UNSUPPORTED_EXTENSION;
        if (total == NXT_TLS_MAX_CERTS) return cert_fail(c, NXT_X509_ERR_CHAIN_TOO_LONG, (int)total);
#ifdef NXT_TESTING
        if (c->test.key_set) { total++; continue; }
#endif
        int prc = nxt_x509_parse(&c->certs[parsed], der.p, der.n);
        if (total == 0 && prc != NXT_X509_OK) leaf_err = prc;
        /* An extra certificate nxtls cannot read (an old cross-sign, say)
         * cannot be on a valid path either, so it is left out. */
        if (prc == NXT_X509_OK) c->cert_index[parsed++] = (uint8_t)total;
        total++;
    }
    c->info.chain_len = (uint8_t)total;

#ifdef NXT_TESTING
    if (c->test.key_set)
        return keep_peer_key(c, c->test.key_type, c->test.key, c->test.key_len,
                             c->test.e, c->test.e_len);
#endif
    if (leaf_err != NXT_X509_OK) return cert_fail(c, leaf_err, 0);

    nxt_verify_opts opts = { c->now, c->host, 0 };
    nxt_verify_result res;
    int vrc = nxt_x509_verify_server(c->certs, parsed, c->roots, &opts, &res);
    if (vrc != NXT_X509_OK) {
        int idx = res.failed_index >= 0 && (size_t)res.failed_index < parsed
                      ? c->cert_index[res.failed_index] : -1;
        return cert_fail(c, vrc, idx);
    }
    c->info.path_len = (uint8_t)res.path_length;
    const nxt_x509_cert *leaf = &c->certs[0];
    return keep_peer_key(c, leaf->key_type, leaf->key.p, leaf->key.len, leaf->rsa_e.p,
                         leaf->rsa_e.len);
}

/* CertificateVerify: the signature over 64 spaces, the context string,
 * a zero byte and Transcript-Hash(ClientHello..Certificate) (4.4.3). */
static int on_certificate_verify(conn_t *c, const uint8_t *m, size_t n, const uint8_t *th) {
    rd r = { m + 4, n - 4 }, sig;
    uint16_t scheme;
    if (!rd_u16(&r, &scheme) || !rd_vec(&r, 2, &sig) || r.n != 0) return NXT_TLS_ERR_DECODE;

    nxc_hash_alg_t h = NXC_HASH_NONE;
    nxt_key_type_t need = NXT_KEY_NONE;
    switch (scheme) {
    case SIG_ECDSA_P256_SHA256: h = NXC_HASH_SHA256; need = NXT_KEY_EC_P256; break;
    case SIG_ECDSA_P384_SHA384: h = NXC_HASH_SHA384; need = NXT_KEY_EC_P384; break;
    case SIG_RSA_PSS_SHA256: h = NXC_HASH_SHA256; need = NXT_KEY_RSA; break;
    case SIG_RSA_PSS_SHA384: h = NXC_HASH_SHA384; need = NXT_KEY_RSA; break;
    case SIG_RSA_PSS_SHA512: h = NXC_HASH_SHA512; need = NXT_KEY_RSA; break;
    case SIG_ED25519: need = NXT_KEY_ED25519; break;
    default: return NXT_TLS_ERR_ILLEGAL_PARAMETER;   /* not offered for CertificateVerify */
    }
    if (need != c->peer_key_type) return NXT_TLS_ERR_ILLEGAL_PARAMETER;

    uint8_t content[64 + sizeof(CV_CONTEXT) + NXT__MAX_HASH];
    for (size_t i = 0; i < 64; i++) content[i] = 0x20;
    copy(content + 64, (const uint8_t *)CV_CONTEXT, sizeof(CV_CONTEXT));   /* with the 0 byte */
    copy(content + 64 + sizeof(CV_CONTEXT), th, c->suite->hash_len);
    size_t clen = 64 + sizeof(CV_CONTEXT) + c->suite->hash_len;

    int ok;
    if (need == NXT_KEY_ED25519) {
        ok = sig.n == NXC_ED25519_SIGNATURE_LEN && c->peer_key_len == NXC_ED25519_PUBLIC_LEN &&
             nxc_ed25519_verify(sig.p, content, clen, c->peer_key) == NXC_OK;
    } else {
        uint8_t digest[NXC_HASH_MAX_DIGEST_LEN];
        size_t dlen = nxc_hash_digest_len(h);
        if (nxc_hash(h, content, clen, digest, sizeof(digest)) != NXC_OK) return NXT_TLS_ERR_CRYPTO;
        if (need == NXT_KEY_RSA)
            ok = nxc_rsa_verify_pss(c->peer_key, c->peer_key_len, c->peer_e, c->peer_e_len, h,
                                    digest, dlen, dlen, sig.p, sig.n) == NXC_OK;
        else
            ok = nxc_ecdsa_verify_der(need == NXT_KEY_EC_P256 ? NXC_EC_P256 : NXC_EC_P384,
                                      c->peer_key, c->peer_key_len, digest, dlen,
                                      sig.p, sig.n) == NXC_OK;
    }
    return ok ? NXT_TLS_OK : NXT_TLS_ERR_BAD_SIGNATURE;
}

static int on_server_finished(conn_t *c, const uint8_t *m, size_t n, const uint8_t *th) {
    if (n - 4 != c->suite->hash_len) return NXT_TLS_ERR_DECODE;
    uint8_t mac[NXT__MAX_HASH];
    int rc = nxt__finished_mac(c->suite, c->ks.server_hs, th, mac);
    if (rc != NXT_TLS_OK) return rc;
    int ok = nxc_ct_equal(mac, m + 4, c->suite->hash_len);
    nxc_memzero(mac, sizeof(mac));
    return ok ? NXT_TLS_OK : NXT_TLS_ERR_BAD_FINISHED;
}

/* The client's second flight: [change_cipher_spec] [Certificate] Finished,
 * then switch both directions to the application traffic keys. */
static int finish_handshake(conn_t *c) {
    uint8_t th_srv[NXT__MAX_HASH], th[NXT__MAX_HASH];
    int rc = th_get(c, th_srv);
    if (rc != NXT_TLS_OK) return rc;

    /* Build the flight in out[] after the record header, sealed in place. */
    uint8_t *body = c->out + NXT_TLS_RECORD_HEADER;
    size_t len = 0;
    if (c->info.cert_requested) {
        /* An empty Certificate: no client certificate (4.4.2). */
        size_t clen = 1 + c->cr_context_len + 3;
        body[0] = HS_CERTIFICATE;
        body[1] = 0;
        body[2] = (uint8_t)(clen >> 8);
        body[3] = (uint8_t)clen;
        body[4] = c->cr_context_len;
        copy(body + 5, c->cr_context, c->cr_context_len);
        body[5 + c->cr_context_len] = 0;
        body[6 + c->cr_context_len] = 0;
        body[7 + c->cr_context_len] = 0;
        len = 4 + clen;
        rc = th_update(c, body, len);
        if (rc != NXT_TLS_OK) return rc;
    }
    rc = th_get(c, th);
    if (rc != NXT_TLS_OK) return rc;
    uint8_t *fin = body + len;
    fin[0] = HS_FINISHED;
    fin[1] = 0;
    fin[2] = 0;
    fin[3] = c->suite->hash_len;
    rc = nxt__finished_mac(c->suite, c->ks.client_hs, th, fin + 4);
    if (rc != NXT_TLS_OK) return rc;
    len += 4 + c->suite->hash_len;

    /* Middlebox compatibility mode (RFC 8446 D.4): a dummy
     * change_cipher_spec before the second flight. */
    if (c->sid_len > 0) {
        static const uint8_t ccs[6] = { NXT_TLS_CT_CHANGE_CIPHER_SPEC, 3, 3, 0, 1, 1 };
        rc = send_all(c, ccs, sizeof(ccs));
        if (rc != NXT_TLS_OK) return rc;
    }
    rc = send_sealed(c, NXT_TLS_CT_HANDSHAKE, body, len);
    if (rc != NXT_TLS_OK) return rc;

    rc = nxt__ks_application(&c->ks, th_srv);
    if (rc != NXT_TLS_OK) return rc;
    copy(c->rx_secret, c->ks.server_ap, c->suite->hash_len);
    copy(c->tx_secret, c->ks.client_ap, c->suite->hash_len);
    nxt__ks_wipe(&c->ks);
    rc = nxt__rprot_init(&c->rx, c->suite, c->rx_secret);
    if (rc == NXT_TLS_OK) rc = nxt__rprot_init(&c->tx, c->suite, c->tx_secret);
    return rc;
}

/* ------------------------------------------------------------------ */
/* Connect                                                              */
/* ------------------------------------------------------------------ */

static int copy_config(conn_t *c, const nxt_tls_config *cfg) {
    if (cfg->host == NULL || cfg->roots == NULL || cfg->io.send == NULL || cfg->io.recv == NULL ||
        cfg->random == NULL || (cfg->roots->anchors == NULL && cfg->roots->count != 0))
        return NXT_TLS_ERR_ARG;
    size_t hl = 0;
    while (cfg->host[hl] != 0) {
        if (++hl > NXT_TLS_MAX_HOST) return NXT_TLS_ERR_ARG;
    }
    const uint8_t *h = (const uint8_t *)cfg->host;
    uint8_t ip[16];
    c->host_is_ip = nxt__parse_ipv4(h, hl, ip) || nxt__parse_ipv6(h, hl, ip);
    if (!c->host_is_ip && !nxt__valid_dns(h, hl, 0)) return NXT_TLS_ERR_ARG;
    copy((uint8_t *)c->host, h, hl);
    c->host[hl] = 0;
    c->host_len = hl;

    if (cfg->alpn != NULL) {
        size_t al = 0;
        while (cfg->alpn[al] != 0) {
            if (++al > NXT_TLS_MAX_ALPN) return NXT_TLS_ERR_ARG;
        }
        if (al == 0) return NXT_TLS_ERR_ARG;
        copy((uint8_t *)c->alpn, (const uint8_t *)cfg->alpn, al);
        c->alpn[al] = 0;
        c->alpn_len = al;
    }
    c->roots = cfg->roots;
    c->now = cfg->now;
    c->io = cfg->io;
    return NXT_TLS_OK;
}

static void reset(conn_t *c) {
#ifdef NXT_TESTING
    __typeof__(c->test) keep = c->test;
#endif
    nxc_memzero(c, sizeof(*c));
#ifdef NXT_TESTING
    c->test = keep;
#endif
    c->magic = CONN_MAGIC;
    c->info.cert_error = NXT_X509_OK;
    c->info.cert_failed_index = -1;
    c->info.alert_sent = -1;
    c->info.alert_received = -1;
    c->peer_limit = NXT_TLS_MAX_PLAINTEXT;
}

/* Expect the next handshake message to be `type`. */
static int expect_hs(conn_t *c, uint8_t type, const uint8_t **m, size_t *n) {
    int rc = next_hs(c, m, n);
    if (rc != NXT_TLS_OK) return rc;
    return (*m)[0] == type ? NXT_TLS_OK : NXT_TLS_ERR_UNEXPECTED_MESSAGE;
}

static int handshake(conn_t *c, const nxt_tls_config *cfg) {
    if (nxc_hash_init(&c->th256, NXC_HASH_SHA256) != NXC_OK ||
        nxc_hash_init(&c->th384, NXC_HASH_SHA384) != NXC_OK)
        return NXT_TLS_ERR_CRYPTO;

    /* ClientHello1: random, a 32-byte session id (middlebox compatibility
     * mode, RFC 8446 D.4) and the X25519 key, all from the caller's
     * random source. */
    int rc;
#ifdef NXT_TESTING
    if (c->test.hello) {
        rc = adopt_test_hello(c);
        if (rc != NXT_TLS_OK) return rc;
        c->state = ST_HANDSHAKE;
        rc = send_hello_record(c, c->test.hello, c->test.hello_len, 1);
    } else
#endif
    {
        uint8_t rnd[96];
        if (cfg->random(cfg->random_ctx, rnd, sizeof(rnd)) != 0) {
            nxc_memzero(rnd, sizeof(rnd));
            return NXT_TLS_ERR_RANDOM;
        }
        copy(c->client_random, rnd, 32);
        copy(c->sid, rnd + 32, 32);
        c->sid_len = 32;
        copy(c->priv, rnd + 64, 32);
        nxc_memzero(rnd, sizeof(rnd));
        wr w = { c->out, 0, sizeof(c->out), 0 };
        build_client_hello(c, &w, NULL, 0);
        if (w.err) return NXT_TLS_ERR_BUFFER;
        c->state = ST_HANDSHAKE;
        rc = send_hello_record(c, c->out, w.n, 1);
    }
    if (rc != NXT_TLS_OK) return rc;

    const uint8_t *m;
    size_t n;
    int retried = 0;
    rc = expect_hs(c, HS_SERVER_HELLO, &m, &n);
    if (rc == NXT_TLS_OK) rc = on_server_hello(c, m, n, &retried);
    if (rc == NXT_TLS_OK && retried) {
        if (hs_pending(c) != 0) return NXT_TLS_ERR_UNEXPECTED_MESSAGE;
        rc = expect_hs(c, HS_SERVER_HELLO, &m, &n);
        if (rc == NXT_TLS_OK) rc = on_server_hello(c, m, n, &retried);
        if (rc == NXT_TLS_OK && retried) rc = NXT_TLS_ERR_UNEXPECTED_MESSAGE;
    }
    if (rc != NXT_TLS_OK) return rc;
    /* The keys change after ServerHello: nothing else may share its record. */
    if (hs_pending(c) != 0) return NXT_TLS_ERR_UNEXPECTED_MESSAGE;

    rc = expect_hs(c, HS_ENCRYPTED_EXTENSIONS, &m, &n);
    if (rc == NXT_TLS_OK) rc = on_encrypted_extensions(c, m, n);
    if (rc == NXT_TLS_OK) rc = th_update(c, m, n);
    if (rc == NXT_TLS_OK) rc = next_hs(c, &m, &n);
    if (rc == NXT_TLS_OK && m[0] == HS_CERTIFICATE_REQUEST) {
        rc = on_certificate_request(c, m, n);
        if (rc == NXT_TLS_OK) rc = th_update(c, m, n);
        if (rc == NXT_TLS_OK) rc = next_hs(c, &m, &n);
    }
    if (rc != NXT_TLS_OK) return rc;
    if (m[0] != HS_CERTIFICATE) return NXT_TLS_ERR_UNEXPECTED_MESSAGE;
    rc = on_certificate(c, m, n);
    if (rc == NXT_TLS_OK) rc = th_update(c, m, n);
    if (rc != NXT_TLS_OK) return rc;

    uint8_t th[NXT__MAX_HASH];
    rc = th_get(c, th);
    if (rc == NXT_TLS_OK) rc = expect_hs(c, HS_CERTIFICATE_VERIFY, &m, &n);
    if (rc == NXT_TLS_OK) rc = on_certificate_verify(c, m, n, th);
    if (rc == NXT_TLS_OK) rc = th_update(c, m, n);
    if (rc == NXT_TLS_OK) rc = th_get(c, th);
    if (rc == NXT_TLS_OK) rc = expect_hs(c, HS_FINISHED, &m, &n);
    if (rc == NXT_TLS_OK) rc = on_server_finished(c, m, n, th);
    if (rc == NXT_TLS_OK) rc = th_update(c, m, n);
    if (rc != NXT_TLS_OK) return rc;
    c->server_finished = 1;
    if (hs_pending(c) != 0) return NXT_TLS_ERR_UNEXPECTED_MESSAGE;

    rc = finish_handshake(c);
    if (rc != NXT_TLS_OK) return rc;
    /* The handshake transcript is no longer needed. */
    nxc_memzero(&c->th256, sizeof(c->th256));
    nxc_memzero(&c->th384, sizeof(c->th384));
    c->state = ST_OPEN;
    return NXT_TLS_OK;
}

int nxt_tls_connect(nxt_tls_conn *conn, const nxt_tls_config *cfg) {
    if (conn == NULL) return NXT_TLS_ERR_ARG;
    conn_t *c = C(conn);
    reset(c);
    if (cfg == NULL) {
        c->state = ST_FAILED;
        return NXT_TLS_ERR_ARG;
    }
    int rc = copy_config(c, cfg);
    if (rc == NXT_TLS_OK) rc = handshake(c, cfg);
    if (rc != NXT_TLS_OK) return fail(c, rc);
    return NXT_TLS_OK;
}

/* ------------------------------------------------------------------ */
/* Application data                                                     */
/* ------------------------------------------------------------------ */

static int update_rx(conn_t *c) {
    int rc = nxt__next_traffic_secret(c->suite, c->rx_secret);
    if (rc == NXT_TLS_OK) rc = nxt__rprot_init(&c->rx, c->suite, c->rx_secret);
    return rc;
}

static int update_tx(conn_t *c) {
    int rc = nxt__next_traffic_secret(c->suite, c->tx_secret);
    if (rc == NXT_TLS_OK) rc = nxt__rprot_init(&c->tx, c->suite, c->tx_secret);
    return rc;
}

/* A handshake message after the handshake: NewSessionTicket (read and
 * dropped) or KeyUpdate. Anything else is unexpected. */
static int on_post_handshake(conn_t *c, const uint8_t *m, size_t n) {
    rd r = { m + 4, n - 4 };
    if (m[0] == HS_NEW_SESSION_TICKET) {
        const uint8_t *p;
        rd nonce, ticket, exts;
        if (!rd_bytes(&r, 8, &p) || !rd_vec(&r, 1, &nonce) || !rd_vec(&r, 2, &ticket) ||
            ticket.n == 0 || !rd_vec(&r, 2, &exts) || r.n != 0)
            return NXT_TLS_ERR_DECODE;
        return NXT_TLS_OK;
    }
    if (m[0] == HS_KEY_UPDATE) {
        uint8_t req;
        if (!rd_u8(&r, &req) || r.n != 0) return NXT_TLS_ERR_DECODE;
        if (req > 1) return NXT_TLS_ERR_ILLEGAL_PARAMETER;
        /* The key changes after this message (RFC 8446 4.6.3). */
        if (hs_pending(c) != 0) return NXT_TLS_ERR_UNEXPECTED_MESSAGE;
        int rc = update_rx(c);
        if (rc == NXT_TLS_OK && req == 1) {
            static const uint8_t ku[5] = { HS_KEY_UPDATE, 0, 0, 1, 0 };   /* update_not_requested */
            rc = send_sealed(c, NXT_TLS_CT_HANDSHAKE, ku, sizeof(ku));
            if (rc == NXT_TLS_OK) rc = update_tx(c);
        }
        return rc;
    }
    return NXT_TLS_ERR_UNEXPECTED_MESSAGE;
}

long nxt_tls_read(nxt_tls_conn *conn, uint8_t *buf, size_t cap) {
    if (conn == NULL || buf == NULL || cap == 0) return NXT_TLS_ERR_ARG;
    conn_t *c = C(conn);
    if (c->magic != CONN_MAGIC) return NXT_TLS_ERR_ARG;
    if (c->state == ST_PEER_CLOSED) return 0;
    if (c->state != ST_OPEN) return NXT_TLS_ERR_ARG;
    for (;;) {
        if (c->app_len > 0) {
            size_t k = c->app_len < cap ? c->app_len : cap;
            copy(buf, c->in + c->app_off, k);
            c->app_off += k;
            c->app_len -= k;
            return (long)k;
        }
        uint8_t type;
        uint8_t *d;
        size_t n;
        int rc = next_content(c, &type, &d, &n);
        if (rc != NXT_TLS_OK) return fail(c, rc);
        if (type == NXT_TLS_CT_APPLICATION_DATA) {
            /* Handshake messages must not be interleaved with other records. */
            if (c->hs_len - c->hs_used != 0) return fail(c, NXT_TLS_ERR_UNEXPECTED_MESSAGE);
            c->app_off = (size_t)(d - c->in);
            c->app_len = n;
            continue;
        }
        if (type == NXT_TLS_CT_ALERT) {
            rc = on_alert(c, d, n);
            if (rc == 0) continue;
            if (rc == 1) {
                c->state = ST_PEER_CLOSED;
                return 0;
            }
            return fail(c, rc);
        }
        /* Handshake data: drop consumed messages, append, handle complete ones. */
        if (c->hs_used > 0) {
            move_down(c->hs, c->hs + c->hs_used, c->hs_len - c->hs_used);
            c->hs_len -= c->hs_used;
            c->hs_used = 0;
        }
        if (n > sizeof(c->hs) - c->hs_len) return fail(c, NXT_TLS_ERR_TOO_LARGE);
        copy(c->hs + c->hs_len, d, n);
        c->hs_len += n;
        while (c->hs_len - c->hs_used >= 4) {
            const uint8_t *m = c->hs + c->hs_used;
            size_t body = (size_t)m[1] << 16 | (size_t)m[2] << 8 | m[3];
            if (body > NXT_TLS_MAX_HANDSHAKE - 4) return fail(c, NXT_TLS_ERR_TOO_LARGE);
            if (c->hs_len - c->hs_used < 4 + body) break;
            c->hs_used += 4 + body;
            rc = on_post_handshake(c, m, 4 + body);
            if (rc != NXT_TLS_OK) return fail(c, rc);
        }
    }
}

long nxt_tls_write(nxt_tls_conn *conn, const uint8_t *data, size_t len) {
    if (conn == NULL || (data == NULL && len != 0)) return NXT_TLS_ERR_ARG;
    conn_t *c = C(conn);
    if (c->magic != CONN_MAGIC) return NXT_TLS_ERR_ARG;
    /* After the server's close_notify the client may still send (half-close). */
    if (c->state != ST_OPEN && c->state != ST_PEER_CLOSED) return NXT_TLS_ERR_ARG;
    if (len > (size_t)0x7fffffff) return NXT_TLS_ERR_ARG;
    size_t done = 0;
    while (done < len) {
        size_t k = len - done;
        if (k > c->peer_limit) k = c->peer_limit;
        int rc = send_sealed(c, NXT_TLS_CT_APPLICATION_DATA, data + done, k);
        if (rc != NXT_TLS_OK) return fail(c, rc);
        done += k;
    }
    return (long)len;
}

int nxt_tls_close(nxt_tls_conn *conn) {
    if (conn == NULL) return NXT_TLS_ERR_ARG;
    conn_t *c = C(conn);
    if (c->magic != CONN_MAGIC) return NXT_TLS_ERR_ARG;
    int rc = NXT_TLS_OK;
    if (c->state == ST_OPEN || c->state == ST_PEER_CLOSED) {
        static const uint8_t cn[2] = { 1, NXT_TLS_ALERT_CLOSE_NOTIFY };
        rc = send_sealed(c, NXT_TLS_CT_ALERT, cn, sizeof(cn));
        if (rc == NXT_TLS_OK) c->info.alert_sent = NXT_TLS_ALERT_CLOSE_NOTIFY;
    }
    wipe_secrets(c);
    c->state = ST_CLOSED;
    return rc;
}

const nxt_tls_info *nxt_tls_get_info(const nxt_tls_conn *conn) {
    static const nxt_tls_info none = { 0, 0, 0, NXT_X509_OK, -1, 0, 0, -1, -1, { 0 } };
    if (conn == NULL) return &none;
    const conn_t *c = (const conn_t *)(const void *)conn->priv.bytes;
    return c->magic == CONN_MAGIC ? &c->info : &none;
}

const char *nxt_tls_alert_str(int alert) {
    switch (alert) {
    case NXT_TLS_ALERT_CLOSE_NOTIFY: return "close_notify";
    case NXT_TLS_ALERT_UNEXPECTED_MESSAGE: return "unexpected_message";
    case NXT_TLS_ALERT_BAD_RECORD_MAC: return "bad_record_mac";
    case NXT_TLS_ALERT_RECORD_OVERFLOW: return "record_overflow";
    case NXT_TLS_ALERT_HANDSHAKE_FAILURE: return "handshake_failure";
    case NXT_TLS_ALERT_BAD_CERTIFICATE: return "bad_certificate";
    case 43: return "unsupported_certificate";
    case 44: return "certificate_revoked";
    case NXT_TLS_ALERT_CERTIFICATE_EXPIRED: return "certificate_expired";
    case NXT_TLS_ALERT_CERTIFICATE_UNKNOWN: return "certificate_unknown";
    case NXT_TLS_ALERT_ILLEGAL_PARAMETER: return "illegal_parameter";
    case NXT_TLS_ALERT_UNKNOWN_CA: return "unknown_ca";
    case 49: return "access_denied";
    case NXT_TLS_ALERT_DECODE_ERROR: return "decode_error";
    case NXT_TLS_ALERT_DECRYPT_ERROR: return "decrypt_error";
    case NXT_TLS_ALERT_PROTOCOL_VERSION: return "protocol_version";
    case 71: return "insufficient_security";
    case NXT_TLS_ALERT_INTERNAL_ERROR: return "internal_error";
    case 86: return "inappropriate_fallback";
    case NXT_TLS_ALERT_USER_CANCELED: return "user_canceled";
    case NXT_TLS_ALERT_MISSING_EXTENSION: return "missing_extension";
    case NXT_TLS_ALERT_UNSUPPORTED_EXTENSION: return "unsupported_extension";
    case 112: return "unrecognized_name";
    case 113: return "bad_certificate_status_response";
    case 115: return "unknown_psk_identity";
    case 116: return "certificate_required";
    case 120: return "no_application_protocol";
    default: return "unknown alert";
    }
}

#ifdef NXT_TESTING
int nxt__tls_test_hello(nxt_tls_conn *conn, const uint8_t *hello, size_t len,
                        const uint8_t priv[32]) {
    if (conn == NULL) return NXT_TLS_ERR_ARG;
    conn_t *c = C(conn);
    nxc_memzero(&c->test, sizeof(c->test));
    c->test.hello = hello;
    c->test.hello_len = len;
    if (priv) copy(c->test.priv, priv, 32);
    return NXT_TLS_OK;
}

int nxt__tls_test_server_key(nxt_tls_conn *conn, nxt_key_type_t type, const uint8_t *key,
                             size_t key_len, const uint8_t *e, size_t e_len) {
    if (conn == NULL || key_len > sizeof(C(conn)->test.key) || e_len > sizeof(C(conn)->test.e))
        return NXT_TLS_ERR_ARG;
    conn_t *c = C(conn);
    c->test.key_set = 1;
    c->test.key_type = type;
    copy(c->test.key, key, key_len);
    c->test.key_len = (uint16_t)key_len;
    copy(c->test.e, e, e_len);
    c->test.e_len = (uint8_t)e_len;
    return NXT_TLS_OK;
}
#endif
