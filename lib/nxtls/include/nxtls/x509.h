/*
 * nxtls - X.509 certificates (RFC 5280) for the TLS 1.3 client.
 *
 * Parsing is zero-copy: a parsed certificate points into the caller's DER
 * buffer, which must stay alive and unchanged while the certificate is in
 * use. Nothing is allocated. Parsing is strict DER and fails closed:
 * anything this code does not understand and that could change the
 * meaning of the certificate (an unknown critical extension, a duplicate
 * extension, a v1/v2 certificate, an unsupported key or signature
 * algorithm) is an error, not something to skip.
 *
 * Supported keys:       RSA 2048-4096 bits, ECDSA P-256 and P-384, Ed25519
 * Supported signatures: RSA PKCS#1 v1.5 and RSA-PSS with SHA-256/384/512,
 *                       ECDSA with SHA-256/384/512, Ed25519
 *
 * Path validation and the host name check are in nxtls/x509_verify.h.
 *
 * Freestanding C11, like lib/nxcrypto: no C library, no allocation, no
 * logging. Callers log the returned code with nxt_x509_err_str().
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#ifndef NXTLS_X509_H
#define NXTLS_X509_H

#include <stddef.h>
#include <stdint.h>

/*
 * Result codes for parsing and verification. 0 is success, failures are
 * negative. Each failure names one reason, so a log line can say exactly
 * why a certificate was refused.
 */
typedef enum {
    NXT_X509_OK                 =   0,
    NXT_X509_ERR_ARG            =  -1,  /* NULL pointer or bad length */
    NXT_X509_ERR_ENCODING       =  -2,  /* not strict DER, or wrong structure */
    NXT_X509_ERR_VERSION        =  -3,  /* not a v3 certificate */
    NXT_X509_ERR_TIME           =  -4,  /* malformed validity time */
    NXT_X509_ERR_KEY            =  -5,  /* malformed or unsupported public key */
    NXT_X509_ERR_WEAK_KEY       =  -6,  /* RSA key shorter than 2048 bits */
    NXT_X509_ERR_SIG_ALG        =  -7,  /* unsupported or mismatched signature algorithm */
    NXT_X509_ERR_CRITICAL_EXT   =  -8,  /* unknown critical extension */
    NXT_X509_ERR_DUPLICATE_EXT  =  -9,  /* the same extension twice */
    NXT_X509_ERR_EXTENSION      = -10,  /* malformed known extension */
    NXT_X509_ERR_BAD_SIGNATURE  = -11,  /* signature does not verify */
    NXT_X509_ERR_EXPIRED        = -12,  /* now is after notAfter */
    NXT_X509_ERR_NOT_YET_VALID  = -13,  /* now is before notBefore */
    NXT_X509_ERR_UNKNOWN_ISSUER = -14,  /* no path to a trusted root (includes self-signed) */
    NXT_X509_ERR_NOT_CA         = -15,  /* issuer is not a CA certificate */
    NXT_X509_ERR_PATH_LEN       = -16,  /* pathLenConstraint exceeded */
    NXT_X509_ERR_KEY_USAGE      = -17,  /* keyUsage forbids this use */
    NXT_X509_ERR_EKU            = -18,  /* extKeyUsage does not allow TLS server auth */
    NXT_X509_ERR_NAME_CONSTRAINTS = -19, /* a name is outside a CA's name constraints */
    NXT_X509_ERR_HOST_MISMATCH  = -20,  /* certificate is not for this host name */
    NXT_X509_ERR_LEAF_IS_CA     = -21,  /* server certificate is a CA certificate */
    NXT_X509_ERR_CHAIN_TOO_LONG = -22,  /* path longer than the depth limit */
    NXT_X509_ERR_BUDGET         = -23,  /* too much path-building work (hostile input) */
    NXT_X509_ERR_BAD_HOST       = -24,  /* the requested host name is not a valid name */
    NXT_X509_ERR_PROFILE        = -25   /* breaks a web PKI rule (CA/Browser Forum BR, RFC 5280 "MUST") */
} nxt_x509_err_t;

/* Short, constant text for a result code, for log lines. */
const char *nxt_x509_err_str(int err);

/* A view of bytes inside the certificate buffer. */
typedef struct {
    const uint8_t *p;
    size_t len;
} nxt_slice;

typedef enum {
    NXT_KEY_NONE = 0,
    NXT_KEY_RSA,
    NXT_KEY_EC_P256,
    NXT_KEY_EC_P384,
    NXT_KEY_ED25519
} nxt_key_type_t;

typedef enum {
    NXT_SIG_NONE = 0,
    NXT_SIG_RSA_PKCS1_SHA256,
    NXT_SIG_RSA_PKCS1_SHA384,
    NXT_SIG_RSA_PKCS1_SHA512,
    NXT_SIG_RSA_PSS_SHA256,
    NXT_SIG_RSA_PSS_SHA384,
    NXT_SIG_RSA_PSS_SHA512,
    NXT_SIG_ECDSA_SHA256,
    NXT_SIG_ECDSA_SHA384,
    NXT_SIG_ECDSA_SHA512,
    NXT_SIG_ED25519
} nxt_sig_alg_t;

/* keyUsage bits (RFC 5280 4.2.1.3), as flags. */
#define NXT_KU_DIGITAL_SIGNATURE 0x0001u
#define NXT_KU_NON_REPUDIATION   0x0002u
#define NXT_KU_KEY_ENCIPHERMENT  0x0004u
#define NXT_KU_DATA_ENCIPHERMENT 0x0008u
#define NXT_KU_KEY_AGREEMENT     0x0010u
#define NXT_KU_KEY_CERT_SIGN     0x0020u
#define NXT_KU_CRL_SIGN          0x0040u
#define NXT_KU_ENCIPHER_ONLY     0x0080u
#define NXT_KU_DECIPHER_ONLY     0x0100u

/* extKeyUsage purposes this code recognises, as flags. */
#define NXT_EKU_SERVER_AUTH      0x0001u
#define NXT_EKU_CLIENT_AUTH      0x0002u
#define NXT_EKU_ANY              0x0004u
#define NXT_EKU_OTHER            0x0008u   /* some purpose not listed above */

/* Which extensions were present. */
#define NXT_EXT_BASIC_CONSTRAINTS 0x0001u
#define NXT_EXT_KEY_USAGE         0x0002u
#define NXT_EXT_EXT_KEY_USAGE     0x0004u
#define NXT_EXT_SUBJECT_ALT_NAME  0x0008u
#define NXT_EXT_NAME_CONSTRAINTS  0x0010u
#define NXT_EXT_SUBJECT_KEY_ID    0x0020u
#define NXT_EXT_AUTHORITY_KEY_ID  0x0040u
#define NXT_EXT_AUTHORITY_INFO    0x0080u   /* authorityInfoAccess (checked for form only) */
#define NXT_EXT_AKI_EXTRA         0x0100u   /* AKI has authorityCertIssuer or serial */

typedef struct {
    nxt_slice raw;              /* the whole Certificate */
    nxt_slice tbs;              /* TBSCertificate, the signed bytes */
    nxt_sig_alg_t sig_alg;      /* outer signatureAlgorithm (equals the TBS one) */
    nxt_slice signature;        /* signature BIT STRING contents, without the unused-bits byte */

    nxt_slice serial;           /* INTEGER contents, big-endian */
    nxt_slice issuer;           /* Name, full DER (tag and length included) */
    nxt_slice subject;          /* Name, full DER */
    int64_t not_before;         /* seconds since 1970-01-01 00:00:00 UTC */
    int64_t not_after;

    nxt_slice spki;             /* SubjectPublicKeyInfo, full DER */
    nxt_key_type_t key_type;
    nxt_slice key;              /* RSA: modulus n (no leading zeros); EC: 04||X||Y; Ed25519: 32 bytes */
    nxt_slice rsa_e;            /* RSA public exponent, else empty */
    uint32_t key_bits;          /* modulus or curve size in bits */

    nxt_slice subject_cn;       /* value of the subject commonName, or empty */
    uint32_t subject_cn_count;  /* number of commonName attributes in the subject */

    uint32_t ext_present;       /* NXT_EXT_* */
    uint32_t ext_critical;      /* NXT_EXT_* that were marked critical */
    int is_ca;                  /* basicConstraints cA */
    int path_len;               /* pathLenConstraint, or -1 for none */
    uint32_t key_usage;         /* NXT_KU_*, valid if NXT_EXT_KEY_USAGE */
    uint32_t ext_key_usage;     /* NXT_EKU_*, valid if NXT_EXT_EXT_KEY_USAGE */
    nxt_slice san;              /* subjectAltName GeneralNames contents (no outer tag) */
    nxt_slice nc_permitted;     /* nameConstraints permittedSubtrees contents, or empty */
    nxt_slice nc_excluded;      /* nameConstraints excludedSubtrees contents, or empty */
    nxt_slice subject_key_id;   /* keyIdentifier contents, or empty */
    nxt_slice authority_key_id; /* keyIdentifier [0] contents, or empty */
} nxt_x509_cert;

/*
 * Parse one DER certificate. On success *cert describes it; on failure
 * *cert is zeroed. The certificate must be exactly der_len bytes (no
 * trailing data). Does not check the signature or the dates.
 */
int nxt_x509_parse(nxt_x509_cert *cert, const uint8_t *der, size_t der_len);

/*
 * Check that `issuer`'s public key signed `cert`. Returns NXT_X509_OK,
 * NXT_X509_ERR_BAD_SIGNATURE, or NXT_X509_ERR_SIG_ALG when the key type
 * does not match the algorithm.
 */
int nxt_x509_check_signature(const nxt_x509_cert *cert, const nxt_x509_cert *issuer);

/* 1 if the two Names are identical (byte for byte), else 0. */
int nxt_x509_name_equal(nxt_slice a, nxt_slice b);

#endif /* NXTLS_X509_H */
