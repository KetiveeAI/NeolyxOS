/*
 * nxtls - TLS 1.3 (RFC 8446) client.
 *
 * A blocking client: nxt_tls_connect() runs the whole handshake over a
 * transport the caller supplies (a connected TCP socket, through two
 * callbacks), then nxt_tls_read() / nxt_tls_write() move application
 * data and nxt_tls_close() ends the session with close_notify.
 *
 * What the client does:
 *  - TLS 1.3 only. A server that cannot speak TLS 1.3 is refused
 *    (NXT_TLS_ERR_PROTOCOL_VERSION) instead of being talked to with a
 *    weaker protocol.
 *  - Key exchange X25519. Cipher suites TLS_CHACHA20_POLY1305_SHA256,
 *    TLS_AES_128_GCM_SHA256 and TLS_AES_256_GCM_SHA384, in that order of
 *    preference.
 *  - Server signatures: ECDSA P-256/SHA-256, ECDSA P-384/SHA-384,
 *    Ed25519 and RSA-PSS (rsae) with SHA-256/384/512.
 *  - The server's certificate chain is validated against the caller's
 *    trust store and host name (nxtls/x509_verify.h: web PKI rules). A
 *    certificate that is expired, self-signed, for another host or not
 *    from a trusted root ends the handshake with NXT_TLS_ERR_CERTIFICATE;
 *    nxt_tls_get_info() then gives the X.509 reason for the log line.
 *  - Server Name Indication for DNS host names, optional ALPN, the
 *    HelloRetryRequest cookie exchange, middlebox compatibility mode,
 *    KeyUpdate in both directions and the record_size_limit extension.
 *  - No client certificates: if the server asks for one, the client
 *    answers with an empty Certificate and the server decides.
 *  - No session resumption, 0-RTT or pre-shared keys. Session tickets
 *    from the server are read and discarded.
 *
 * Timeouts belong to the transport: a recv callback that gives up
 * returns a negative value and the call fails with NXT_TLS_ERR_IO.
 *
 * Freestanding C11, like lib/nxcrypto: no C library, no allocation, no
 * logging. The caller provides the connection's memory (nxt_tls_conn)
 * and logs the returned codes with nxt_tls_err_str().
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#ifndef NXTLS_TLS_H
#define NXTLS_TLS_H

#include <stddef.h>
#include <stdint.h>
#include "nxtls/x509_verify.h"

/* Result codes. 0 is success, failures are negative and each names one
 * reason, so a log line can say exactly why a connection failed. */
typedef enum {
    NXT_TLS_OK                     =   0,
    NXT_TLS_ERR_ARG                =  -1,  /* NULL pointer, bad length or wrong state */
    NXT_TLS_ERR_BUFFER             =  -2,  /* output buffer too small */
    NXT_TLS_ERR_RECORD_OVERFLOW    =  -3,  /* record longer than RFC 8446 5.2 allows */
    NXT_TLS_ERR_BAD_RECORD_MAC     =  -4,  /* record failed authentication (tampered or wrong key) */
    NXT_TLS_ERR_SEQ_EXHAUSTED      =  -5,  /* 2^64 - 1 records sent or received with one key */
    NXT_TLS_ERR_UNEXPECTED_MESSAGE =  -6,  /* message or record not allowed here */
    NXT_TLS_ERR_CRYPTO             =  -7,  /* a crypto primitive failed (should not happen) */
    NXT_TLS_ERR_IO                 =  -8,  /* the transport failed or timed out */
    NXT_TLS_ERR_CLOSED             =  -9,  /* the transport closed without close_notify */
    NXT_TLS_ERR_DECODE             = -10,  /* malformed message */
    NXT_TLS_ERR_ILLEGAL_PARAMETER  = -11,  /* a field has a value that is not allowed */
    NXT_TLS_ERR_PROTOCOL_VERSION   = -12,  /* the server does not speak TLS 1.3 */
    NXT_TLS_ERR_HANDSHAKE_FAILURE  = -13,  /* no common parameters, or the server gave up */
    NXT_TLS_ERR_CERTIFICATE        = -14,  /* certificate refused; see nxt_tls_info.cert_error */
    NXT_TLS_ERR_BAD_SIGNATURE      = -15,  /* CertificateVerify signature does not verify */
    NXT_TLS_ERR_BAD_FINISHED       = -16,  /* server Finished does not verify */
    NXT_TLS_ERR_ALERT              = -17,  /* the server sent a fatal alert; see alert_received */
    NXT_TLS_ERR_UNSUPPORTED_EXTENSION = -18, /* the server answered an extension that was not offered */
    NXT_TLS_ERR_MISSING_EXTENSION  = -19,  /* a required extension is missing */
    NXT_TLS_ERR_RANDOM             = -20,  /* the random source failed */
    NXT_TLS_ERR_TOO_LARGE          = -21   /* handshake message larger than NXT_TLS_MAX_HANDSHAKE */
} nxt_tls_err_t;

/* Short, constant text for a result code (for the caller's log line). */
const char *nxt_tls_err_str(int err);

/* TLS 1.3 cipher suites (RFC 8446 appendix B.4) supported by nxtls. */
#define NXT_TLS_AES_128_GCM_SHA256       0x1301u
#define NXT_TLS_AES_256_GCM_SHA384       0x1302u
#define NXT_TLS_CHACHA20_POLY1305_SHA256 0x1303u

/* Record sizes (RFC 8446 section 5). */
#define NXT_TLS_MAX_PLAINTEXT   16384u                          /* 2^14 */
#define NXT_TLS_MAX_CIPHERTEXT  (NXT_TLS_MAX_PLAINTEXT + 256u)  /* 2^14 + 256 */
#define NXT_TLS_RECORD_HEADER   5u
#define NXT_TLS_MAX_RECORD      (NXT_TLS_RECORD_HEADER + NXT_TLS_MAX_CIPHERTEXT)

/* Record content types (RFC 8446 section 5.1). */
#define NXT_TLS_CT_CHANGE_CIPHER_SPEC 20u
#define NXT_TLS_CT_ALERT              21u
#define NXT_TLS_CT_HANDSHAKE          22u
#define NXT_TLS_CT_APPLICATION_DATA   23u

/* Alert descriptions (RFC 8446 section 6) that nxtls sends or names. */
#define NXT_TLS_ALERT_CLOSE_NOTIFY          0u
#define NXT_TLS_ALERT_UNEXPECTED_MESSAGE   10u
#define NXT_TLS_ALERT_BAD_RECORD_MAC       20u
#define NXT_TLS_ALERT_RECORD_OVERFLOW      22u
#define NXT_TLS_ALERT_HANDSHAKE_FAILURE    40u
#define NXT_TLS_ALERT_BAD_CERTIFICATE      42u
#define NXT_TLS_ALERT_CERTIFICATE_UNKNOWN  46u
#define NXT_TLS_ALERT_CERTIFICATE_EXPIRED  45u
#define NXT_TLS_ALERT_ILLEGAL_PARAMETER    47u
#define NXT_TLS_ALERT_UNKNOWN_CA           48u
#define NXT_TLS_ALERT_DECODE_ERROR         50u
#define NXT_TLS_ALERT_DECRYPT_ERROR        51u
#define NXT_TLS_ALERT_PROTOCOL_VERSION     70u
#define NXT_TLS_ALERT_INTERNAL_ERROR       80u
#define NXT_TLS_ALERT_USER_CANCELED        90u
#define NXT_TLS_ALERT_MISSING_EXTENSION   109u
#define NXT_TLS_ALERT_UNSUPPORTED_EXTENSION 110u

/* Short, constant name for an alert description, e.g. "bad_certificate". */
const char *nxt_tls_alert_str(int alert);

/* Largest handshake message accepted (a server's Certificate chain). */
#define NXT_TLS_MAX_HANDSHAKE   32768u
/* Most certificates read from the server's Certificate message. */
#define NXT_TLS_MAX_CERTS       16u
/* Longest host name (RFC 1035) and ALPN protocol name accepted. */
#define NXT_TLS_MAX_HOST        253u
#define NXT_TLS_MAX_ALPN        32u

/*
 * The transport. send writes up to len bytes and returns how many (> 0)
 * or a negative value on error. recv reads up to cap bytes and returns
 * how many (> 0), 0 at end of stream, or a negative value on error or
 * timeout. Both may be called with the same ctx from one thread only.
 */
typedef struct {
    void *ctx;
    long (*send)(void *ctx, const uint8_t *data, size_t len);
    long (*recv)(void *ctx, uint8_t *buf, size_t cap);
} nxt_tls_io;

/* Fill out with len cryptographically secure random bytes (on NeolyxOS:
 * the random.get syscall). Return 0 on success, anything else on failure. */
typedef int (*nxt_tls_random_fn)(void *ctx, uint8_t *out, size_t len);

typedef struct {
    const char *host;               /* DNS name or IP literal; SNI and certificate check. Required. */
    const nxt_trust_store *roots;   /* trusted root certificates. Required, may be empty. */
    int64_t now;                    /* current time, seconds since 1970 UTC */
    nxt_tls_io io;                  /* required */
    nxt_tls_random_fn random;       /* required */
    void *random_ctx;
    const char *alpn;               /* e.g. "http/1.1", or NULL to not use ALPN */
} nxt_tls_config;

/* What happened, for the caller's log line. Valid after nxt_tls_connect
 * returns, whether it succeeded or not. */
typedef struct {
    uint16_t suite;            /* negotiated cipher suite, 0 if none yet */
    uint8_t hello_retry;       /* 1 if the server sent a HelloRetryRequest */
    uint8_t cert_requested;    /* 1 if the server asked for a client certificate */
    int cert_error;            /* NXT_X509_* reason when the result is NXT_TLS_ERR_CERTIFICATE */
    int cert_failed_index;     /* index in the server's chain of the refused certificate, or -1 */
    uint8_t chain_len;         /* certificates the server sent */
    uint8_t path_len;          /* certificates on the validated path, leaf included */
    int alert_sent;            /* alert description sent to the server, or -1 */
    int alert_received;        /* alert description received from the server, or -1 */
    char alpn[NXT_TLS_MAX_ALPN + 1]; /* protocol the server selected, or "" */
} nxt_tls_info;

/* Memory for one connection, provided by the caller (static or on the
 * heap; it is large, so not on a small stack). Its contents are private. */
#define NXT_TLS_CONN_SIZE 98304u
typedef struct {
    union { uint64_t align; uint8_t bytes[NXT_TLS_CONN_SIZE]; } priv;
} nxt_tls_conn;

/*
 * Run the handshake. Returns NXT_TLS_OK when the server is authenticated
 * and application data can flow. On failure a fatal alert is sent to the
 * server when the protocol calls for one, every secret is wiped, and the
 * connection can only be passed to nxt_tls_get_info() or reused with a
 * new nxt_tls_connect(). cfg is copied; the strings and the trust store
 * it points to must stay valid until this call returns.
 */
int nxt_tls_connect(nxt_tls_conn *conn, const nxt_tls_config *cfg);

/*
 * Read application data. Returns the number of bytes read (> 0), 0 when
 * the server ended the session with close_notify, or a negative
 * nxt_tls_err_t. NXT_TLS_ERR_CLOSED means the transport ended without
 * close_notify, so the data may have been cut short.
 */
long nxt_tls_read(nxt_tls_conn *conn, uint8_t *buf, size_t cap);

/* Write all len bytes of application data. Returns len or a negative nxt_tls_err_t. */
long nxt_tls_write(nxt_tls_conn *conn, const uint8_t *data, size_t len);

/* Send close_notify (if the session is still open) and wipe the connection. */
int nxt_tls_close(nxt_tls_conn *conn);

/* Information about the handshake (see nxt_tls_info). Never NULL. */
const nxt_tls_info *nxt_tls_get_info(const nxt_tls_conn *conn);

#endif /* NXTLS_TLS_H */
