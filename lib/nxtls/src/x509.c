/*
 * nxtls - X.509 v3 certificate parsing and signature check (RFC 5280).
 *
 * Algorithm identifiers are matched as whole DER encodings against the
 * short list below, so parameters can never be misread: anything that
 * is not byte-for-byte one of these is unsupported.
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include "nxtls/x509.h"
#include "nxcrypto.h"
#include "der.h"
#include "names.h"

/* ------------------------------------------------------------------ */
/* Object identifiers and algorithm identifiers (DER)                  */
/* ------------------------------------------------------------------ */

/* AlgorithmIdentifier SEQUENCE { OID 1.2.840.113549.1.1.{11,12,13}, NULL } */
static const uint8_t AI_RSA_SHA256[] = { 0x30, 0x0d, 0x06, 0x09, 0x2a, 0x86, 0x48, 0x86, 0xf7,
                                         0x0d, 0x01, 0x01, 0x0b, 0x05, 0x00 };
static const uint8_t AI_RSA_SHA384[] = { 0x30, 0x0d, 0x06, 0x09, 0x2a, 0x86, 0x48, 0x86, 0xf7,
                                         0x0d, 0x01, 0x01, 0x0c, 0x05, 0x00 };
static const uint8_t AI_RSA_SHA512[] = { 0x30, 0x0d, 0x06, 0x09, 0x2a, 0x86, 0x48, 0x86, 0xf7,
                                         0x0d, 0x01, 0x01, 0x0d, 0x05, 0x00 };
/* SEQUENCE { OID 1.2.840.10045.4.3.{2,3,4} } (no parameters, RFC 5758) */
static const uint8_t AI_ECDSA_SHA256[] = { 0x30, 0x0a, 0x06, 0x08, 0x2a, 0x86, 0x48, 0xce,
                                           0x3d, 0x04, 0x03, 0x02 };
static const uint8_t AI_ECDSA_SHA384[] = { 0x30, 0x0a, 0x06, 0x08, 0x2a, 0x86, 0x48, 0xce,
                                           0x3d, 0x04, 0x03, 0x03 };
static const uint8_t AI_ECDSA_SHA512[] = { 0x30, 0x0a, 0x06, 0x08, 0x2a, 0x86, 0x48, 0xce,
                                           0x3d, 0x04, 0x03, 0x04 };
/* SEQUENCE { OID 1.3.101.112 } (RFC 8410) */
static const uint8_t AI_ED25519[] = { 0x30, 0x05, 0x06, 0x03, 0x2b, 0x65, 0x70 };

/*
 * RSASSA-PSS (OID 1.2.840.113549.1.1.10) with RSASSA-PSS-params
 *   [0] hashAlgorithm      SEQUENCE { sha-N OID, NULL }
 *   [1] maskGenAlgorithm   SEQUENCE { mgf1 OID 1.2.840.113549.1.1.8, SEQUENCE { sha-N, NULL } }
 *   [2] saltLength         N/8
 * the form RFC 4055 section 3.1 and CA/Browser Forum certificates use.
 */
#define PSS_AI(h, salt) { 0x30, 0x41, 0x06, 0x09, 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, \
    0x0a, 0x30, 0x34, 0xa0, 0x0f, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x65, 0x03,  \
    0x04, 0x02, h, 0x05, 0x00, 0xa1, 0x1c, 0x30, 0x1a, 0x06, 0x09, 0x2a, 0x86, 0x48, 0x86,      \
    0xf7, 0x0d, 0x01, 0x01, 0x08, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x65, 0x03,  \
    0x04, 0x02, h, 0x05, 0x00, 0xa2, 0x03, 0x02, 0x01, salt }
static const uint8_t AI_PSS_SHA256[] = PSS_AI(0x01, 0x20);
static const uint8_t AI_PSS_SHA384[] = PSS_AI(0x02, 0x30);
static const uint8_t AI_PSS_SHA512[] = PSS_AI(0x03, 0x40);

static const struct {
    const uint8_t *der;
    size_t len;
    nxt_sig_alg_t alg;
} SIG_ALGS[] = {
    { AI_RSA_SHA256, sizeof(AI_RSA_SHA256), NXT_SIG_RSA_PKCS1_SHA256 },
    { AI_RSA_SHA384, sizeof(AI_RSA_SHA384), NXT_SIG_RSA_PKCS1_SHA384 },
    { AI_RSA_SHA512, sizeof(AI_RSA_SHA512), NXT_SIG_RSA_PKCS1_SHA512 },
    { AI_PSS_SHA256, sizeof(AI_PSS_SHA256), NXT_SIG_RSA_PSS_SHA256 },
    { AI_PSS_SHA384, sizeof(AI_PSS_SHA384), NXT_SIG_RSA_PSS_SHA384 },
    { AI_PSS_SHA512, sizeof(AI_PSS_SHA512), NXT_SIG_RSA_PSS_SHA512 },
    { AI_ECDSA_SHA256, sizeof(AI_ECDSA_SHA256), NXT_SIG_ECDSA_SHA256 },
    { AI_ECDSA_SHA384, sizeof(AI_ECDSA_SHA384), NXT_SIG_ECDSA_SHA384 },
    { AI_ECDSA_SHA512, sizeof(AI_ECDSA_SHA512), NXT_SIG_ECDSA_SHA512 },
    { AI_ED25519, sizeof(AI_ED25519), NXT_SIG_ED25519 },
};

/* SubjectPublicKeyInfo algorithms. */
/* rsaEncryption 1.2.840.113549.1.1.1 with NULL parameters */
static const uint8_t AI_KEY_RSA[] = { 0x30, 0x0d, 0x06, 0x09, 0x2a, 0x86, 0x48, 0x86, 0xf7,
                                      0x0d, 0x01, 0x01, 0x01, 0x05, 0x00 };
/* id-ecPublicKey 1.2.840.10045.2.1, namedCurve prime256v1 1.2.840.10045.3.1.7 */
static const uint8_t AI_KEY_P256[] = { 0x30, 0x13, 0x06, 0x07, 0x2a, 0x86, 0x48, 0xce, 0x3d,
                                       0x02, 0x01, 0x06, 0x08, 0x2a, 0x86, 0x48, 0xce, 0x3d,
                                       0x03, 0x01, 0x07 };
/* id-ecPublicKey, namedCurve secp384r1 1.3.132.0.34 */
static const uint8_t AI_KEY_P384[] = { 0x30, 0x10, 0x06, 0x07, 0x2a, 0x86, 0x48, 0xce, 0x3d,
                                       0x02, 0x01, 0x06, 0x05, 0x2b, 0x81, 0x04, 0x00, 0x22 };

/* Extension OIDs: id-ce 2.5.29.n is 55 1d n. */
#define ID_CE(n) { 0x55, 0x1d, n }
static const uint8_t OID_SUBJECT_KEY_ID[]   = ID_CE(14);
static const uint8_t OID_KEY_USAGE[]        = ID_CE(15);
static const uint8_t OID_SUBJECT_ALT_NAME[] = ID_CE(17);
static const uint8_t OID_BASIC_CONSTRAINTS[] = ID_CE(19);
static const uint8_t OID_NAME_CONSTRAINTS[] = ID_CE(30);
static const uint8_t OID_AUTHORITY_KEY_ID[] = ID_CE(35);
static const uint8_t OID_EXT_KEY_USAGE[]    = ID_CE(37);
static const uint8_t OID_POLICY_CONSTRAINTS[] = ID_CE(36);
/* authorityInfoAccess 1.3.6.1.5.5.7.1.1 */
static const uint8_t OID_AUTHORITY_INFO[] = { 0x2b, 0x06, 0x01, 0x05, 0x05, 0x07, 0x01, 0x01 };
/* commonName 2.5.4.3 */
static const uint8_t OID_COMMON_NAME[] = { 0x55, 0x04, 0x03 };

/* extKeyUsage purposes */
static const uint8_t OID_EKU_SERVER[] = { 0x2b, 0x06, 0x01, 0x05, 0x05, 0x07, 0x03, 0x01 };
static const uint8_t OID_EKU_CLIENT[] = { 0x2b, 0x06, 0x01, 0x05, 0x05, 0x07, 0x03, 0x02 };
static const uint8_t OID_EKU_ANY[]    = { 0x55, 0x1d, 0x25, 0x00 };

#define MAX_EXTENSIONS 64

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

static nxt_slice slice(const der_reader *r) {
    nxt_slice s = { r->p, r->len };
    return s;
}

static void zero_cert(nxt_x509_cert *c) {
    uint8_t *p = (uint8_t *)c;
    for (size_t i = 0; i < sizeof(*c); i++) p[i] = 0;
    c->path_len = -1;
}

int nxt_x509_name_equal(nxt_slice a, nxt_slice b) {
    if (a.len != b.len || a.p == NULL || b.p == NULL) return 0;
    for (size_t i = 0; i < a.len; i++)
        if (a.p[i] != b.p[i]) return 0;
    return 1;
}

/* ------------------------------------------------------------------ */
/* Time                                                                */
/* ------------------------------------------------------------------ */

/* Days from 1970-01-01 to y-m-d in the proleptic Gregorian calendar. */
static int64_t days_from_civil(int64_t y, unsigned m, unsigned d) {
    y -= m <= 2;
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153 * (m + (m > 2 ? (unsigned)-3 : 9)) + 2) / 5 + d - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int64_t)doe - 719468;
}

static int digits(const uint8_t *p, size_t n, unsigned *out) {
    unsigned v = 0;
    for (size_t i = 0; i < n; i++) {
        if (p[i] < '0' || p[i] > '9') return -1;
        v = v * 10 + (unsigned)(p[i] - '0');
    }
    *out = v;
    return 0;
}

/*
 * UTCTime YYMMDDHHMMSSZ or GeneralizedTime YYYYMMDDHHMMSSZ, the only
 * forms RFC 5280 4.1.2.5 allows (UTC, seconds present, no fractions).
 */
static int parse_time(der_reader *r, int64_t *out) {
    uint8_t tag;
    der_reader c;
    if (der_next(r, &tag, &c, NULL) != 0) return -1;
    unsigned year, mon, day, hh, mm, ss;
    const uint8_t *p = c.p;
    if (tag == DER_UTC_TIME) {
        if (c.len != 13 || p[12] != 'Z' || digits(p, 2, &year) != 0) return -1;
        year += year >= 50 ? 1900 : 2000;
        p += 2;
    } else if (tag == DER_GEN_TIME) {
        if (c.len != 15 || p[14] != 'Z' || digits(p, 4, &year) != 0) return -1;
        p += 4;
    } else {
        return -1;
    }
    if (digits(p, 2, &mon) || digits(p + 2, 2, &day) || digits(p + 4, 2, &hh) ||
        digits(p + 6, 2, &mm) || digits(p + 8, 2, &ss))
        return -1;
    static const uint8_t mdays[12] = { 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    int leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    if (mon < 1 || mon > 12 || day < 1 || day > mdays[mon - 1] ||
        (mon == 2 && day == 29 && !leap) || hh > 23 || mm > 59 || ss > 59)
        return -1;
    *out = days_from_civil(year, mon, day) * 86400 + (int64_t)hh * 3600 + mm * 60 + ss;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Names                                                               */
/* ------------------------------------------------------------------ */

/*
 * Name ::= SEQUENCE OF SET SIZE (1..MAX) OF SEQUENCE { OID, ANY }.
 * If c is not NULL, records the subject commonName (value bytes of the
 * first one, and how many there are).
 */
static int check_name(const der_reader *name_content, nxt_x509_cert *c) {
    der_reader rdns = *name_content;
    while (!der_empty(&rdns)) {
        der_reader set;
        if (der_expect(&rdns, DER_SET, &set, NULL) != 0 || der_empty(&set)) return -1;
        while (!der_empty(&set)) {
            der_reader atv, oid, value;
            uint8_t tag;
            if (der_expect(&set, DER_SEQUENCE, &atv, NULL) != 0 ||
                der_expect(&atv, DER_OID, &oid, NULL) != 0 || oid.len == 0 ||
                der_next(&atv, &tag, &value, NULL) != 0 || !der_empty(&atv))
                return -1;
            if (c != NULL && der_equal(&oid, OID_COMMON_NAME, sizeof(OID_COMMON_NAME))) {
                if (c->subject_cn_count++ == 0) c->subject_cn = slice(&value);
            }
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Public key                                                          */
/* ------------------------------------------------------------------ */

static int parse_spki(nxt_x509_cert *c, der_reader *tbs) {
    der_reader spki, whole, alg, alg_whole, bits;
    unsigned unused;
    if (der_expect(tbs, DER_SEQUENCE, &spki, &whole) != 0 ||
        der_expect(&spki, DER_SEQUENCE, &alg, &alg_whole) != 0 ||
        der_bit_string(&spki, &bits, &unused) != 0 || unused != 0 || !der_empty(&spki))
        return NXT_X509_ERR_ENCODING;
    c->spki = slice(&whole);

    if (der_equal(&alg_whole, AI_KEY_P256, sizeof(AI_KEY_P256))) {
        if (bits.len != 65 || bits.p[0] != 0x04) return NXT_X509_ERR_KEY;
        c->key_type = NXT_KEY_EC_P256;
        c->key_bits = 256;
    } else if (der_equal(&alg_whole, AI_KEY_P384, sizeof(AI_KEY_P384))) {
        if (bits.len != 97 || bits.p[0] != 0x04) return NXT_X509_ERR_KEY;
        c->key_type = NXT_KEY_EC_P384;
        c->key_bits = 384;
    } else if (der_equal(&alg_whole, AI_ED25519, sizeof(AI_ED25519))) {
        if (bits.len != 32) return NXT_X509_ERR_KEY;
        c->key_type = NXT_KEY_ED25519;
        c->key_bits = 256;
    } else if (der_equal(&alg_whole, AI_KEY_RSA, sizeof(AI_KEY_RSA))) {
        /* RSAPublicKey ::= SEQUENCE { modulus INTEGER, publicExponent INTEGER } */
        der_reader rsa, n, e, n_mag, e_mag;
        if (der_expect(&bits, DER_SEQUENCE, &rsa, NULL) != 0 || !der_empty(&bits) ||
            der_expect(&rsa, DER_INTEGER, &n, NULL) != 0 || der_integer(&n, 0, &n_mag) != 0 ||
            der_expect(&rsa, DER_INTEGER, &e, NULL) != 0 || der_integer(&e, 0, &e_mag) != 0 ||
            !der_empty(&rsa))
            return NXT_X509_ERR_KEY;
        if (n_mag.len == 0 || n_mag.p[0] == 0) return NXT_X509_ERR_KEY;
        uint32_t nbits = (uint32_t)n_mag.len * 8;
        for (uint8_t top = n_mag.p[0]; !(top & 0x80); top <<= 1) nbits--;
        if (nbits < 2048) return NXT_X509_ERR_WEAK_KEY;
        /* Mozilla root store policy 5.1: modulus size a multiple of 8 bits. */
        if (nbits > NXC_RSA_MAX_BITS || nbits % 8 != 0 || !(n_mag.p[n_mag.len - 1] & 1))
            return NXT_X509_ERR_KEY;
        /* e: odd, at least 3, at most 64 bits */
        if (e_mag.len == 0 || e_mag.len > 8 || !(e_mag.p[e_mag.len - 1] & 1) ||
            (e_mag.len == 1 && e_mag.p[0] < 3))
            return NXT_X509_ERR_KEY;
        c->key_type = NXT_KEY_RSA;
        c->key_bits = nbits;
        c->rsa_e = slice(&e_mag);
        bits = n_mag;
    } else {
        return NXT_X509_ERR_KEY;
    }
    c->key = slice(&bits);
    return NXT_X509_OK;
}

static int parse_sig_alg(der_reader *r, nxt_sig_alg_t *alg, der_reader *whole) {
    der_reader content;
    if (der_expect(r, DER_SEQUENCE, &content, whole) != 0) return NXT_X509_ERR_ENCODING;
    for (size_t i = 0; i < sizeof(SIG_ALGS) / sizeof(SIG_ALGS[0]); i++) {
        if (der_equal(whole, SIG_ALGS[i].der, SIG_ALGS[i].len)) {
            *alg = SIG_ALGS[i].alg;
            return NXT_X509_OK;
        }
    }
    return NXT_X509_ERR_SIG_ALG;
}

/* ------------------------------------------------------------------ */
/* Extensions                                                          */
/* ------------------------------------------------------------------ */

static int ext_basic_constraints(nxt_x509_cert *c, der_reader v, int critical) {
    (void)critical;             /* CA certificates must mark it critical: checked on the path */
    der_reader seq;
    if (der_expect(&v, DER_SEQUENCE, &seq, NULL) != 0 || !der_empty(&v)) return -1;
    if (der_peek(&seq) == DER_BOOLEAN) {
        int ca;
        if (der_boolean(&seq, &ca) != 0 || !ca) return -1;  /* DER: FALSE is never encoded */
        c->is_ca = 1;
    }
    if (der_peek(&seq) == DER_INTEGER) {
        uint32_t pl;
        if (der_small_uint(&seq, 255, &pl) != 0) return -1;
        /* RFC 5280 4.2.1.9: only meaningful when cA is set. */
        if (c->is_ca) c->path_len = (int)pl;
    }
    return der_empty(&seq) ? 0 : -1;
}

static int ext_key_usage(nxt_x509_cert *c, der_reader v, int critical) {
    (void)critical;
    der_reader bits;
    unsigned unused;
    if (der_bit_string(&v, &bits, &unused) != 0 || !der_empty(&v) || bits.len == 0 ||
        bits.len > 2)
        return -1;
    /* DER (X.690 11.2.2): no trailing zero bits in a named-bit list. */
    if (bits.p[bits.len - 1] == 0) return -1;
    if (((bits.p[bits.len - 1] >> unused) & 1u) == 0) return -1;
    uint32_t ku = 0;
    for (unsigned i = 0; i < 9 && i / 8 < bits.len; i++)
        if (bits.p[i / 8] & (0x80u >> (i % 8))) ku |= 1u << i;
    if (bits.len == 2 && (bits.p[1] & 0x7f)) return -1;     /* bits past decipherOnly */
    c->key_usage = ku;
    return 0;
}

static int ext_ext_key_usage(nxt_x509_cert *c, der_reader v, int critical) {
    (void)critical;             /* subscriber certificates: checked on the path */
    der_reader seq;
    if (der_expect(&v, DER_SEQUENCE, &seq, NULL) != 0 || !der_empty(&v) || der_empty(&seq))
        return -1;
    uint32_t eku = 0;
    while (!der_empty(&seq)) {
        der_reader oid;
        if (der_expect(&seq, DER_OID, &oid, NULL) != 0 || oid.len == 0) return -1;
        if (der_equal(&oid, OID_EKU_SERVER, sizeof(OID_EKU_SERVER))) eku |= NXT_EKU_SERVER_AUTH;
        else if (der_equal(&oid, OID_EKU_CLIENT, sizeof(OID_EKU_CLIENT))) eku |= NXT_EKU_CLIENT_AUTH;
        else if (der_equal(&oid, OID_EKU_ANY, sizeof(OID_EKU_ANY))) eku |= NXT_EKU_ANY;
        else eku |= NXT_EKU_OTHER;
    }
    c->ext_key_usage = eku;
    return 0;
}

#define GN_DNS  DER_CTX(2)         /* dNSName, IA5String */
#define GN_IP   DER_CTX(7)         /* iPAddress, OCTET STRING */

/* 1 if the mask (len bytes) is a run of 1 bits followed by 0 bits. */
static int contiguous_mask(const uint8_t *m, size_t len) {
    int zero_seen = 0;
    for (size_t i = 0; i < len; i++) {
        for (int b = 7; b >= 0; b--) {
            int bit = (m[i] >> b) & 1;
            if (bit && zero_seen) return 0;
            if (!bit) zero_seen = 1;
        }
    }
    return 1;
}

/*
 * GeneralNames ::= SEQUENCE SIZE (1..MAX) OF GeneralName, each a
 * context-tagged element. The two forms this code matches on are checked
 * here: a dNSName must be valid DNS syntax; an iPAddress must be 4 or 16 bytes in a
 * subjectAltName, and an address plus a contiguous mask (8 or 32 bytes)
 * in name constraints (RFC 5280 4.2.1.10).
 */
static int check_general_names(der_reader names, int in_constraints) {
    while (!der_empty(&names)) {
        uint8_t tag;
        der_reader v;
        if (der_next(&names, &tag, &v, NULL) != 0 || (tag & 0xc0) != 0x80 || (tag & 0x1f) > 8)
            return -1;
        if (tag == GN_DNS) {
            /* SAN: a host name or "*.name"; constraint: empty or a name
             * (RFC 5280 4.2.1.10 allows no leading period for dNSName). */
            if (in_constraints ? (v.len != 0 && !nxt__valid_dns(v.p, v.len, 0))
                               : !nxt__valid_dns(v.p, v.len, 1))
                return -1;
        } else if (tag == GN_IP) {
            if (in_constraints) {
                if (v.len != 8 && v.len != 32) return -1;
                if (!contiguous_mask(v.p + v.len / 2, v.len / 2)) return -1;
            } else if (v.len != 4 && v.len != 16) {
                return -1;
            }
        }
    }
    return 0;
}

static int ext_subject_alt_name(nxt_x509_cert *c, der_reader v, int critical) {
    (void)critical;             /* must be critical if the subject is empty: checked below */
    der_reader seq;
    if (der_expect(&v, DER_SEQUENCE, &seq, NULL) != 0 || !der_empty(&v) || der_empty(&seq) ||
        check_general_names(seq, 0) != 0)
        return -1;
    c->san = slice(&seq);
    return 0;
}

/* GeneralSubtrees ::= SEQUENCE SIZE (1..MAX) OF SEQUENCE { GeneralName, [0] min DEFAULT 0, [1] max } */
static int check_subtrees(der_reader trees) {
    if (der_empty(&trees)) return -1;
    while (!der_empty(&trees)) {
        der_reader st, gn;
        uint8_t tag;
        if (der_expect(&trees, DER_SEQUENCE, &st, NULL) != 0 ||
            der_next(&st, &tag, NULL, &gn) != 0 || check_general_names(gn, 1) != 0 ||
            !der_empty(&st))  /* RFC 5280 4.2.1.10: minimum is 0 (absent), maximum absent */
            return -1;
    }
    return 0;
}

static int ext_name_constraints(nxt_x509_cert *c, der_reader v, int critical) {
    der_reader seq, perm, excl;
    int has_perm, has_excl;
    if (!critical) return -1;   /* RFC 5280 4.2.1.10: MUST be critical */
    if (der_expect(&v, DER_SEQUENCE, &seq, NULL) != 0 || !der_empty(&v) ||
        der_optional(&seq, DER_CTX_CONS(0), &perm, &has_perm) != 0 ||
        der_optional(&seq, DER_CTX_CONS(1), &excl, &has_excl) != 0 || !der_empty(&seq) ||
        (!has_perm && !has_excl))
        return -1;
    if (has_perm) {
        if (check_subtrees(perm) != 0) return -1;
        c->nc_permitted = slice(&perm);
    }
    if (has_excl) {
        if (check_subtrees(excl) != 0) return -1;
        c->nc_excluded = slice(&excl);
    }
    return 0;
}

static int ext_subject_key_id(nxt_x509_cert *c, der_reader v, int critical) {
    der_reader id;
    if (critical) return -1;    /* RFC 5280 4.2.1.2: MUST NOT be critical */
    if (der_expect(&v, DER_OCTET_STRING, &id, NULL) != 0 || !der_empty(&v)) return -1;
    c->subject_key_id = slice(&id);
    return 0;
}

/* AuthorityKeyIdentifier ::= SEQUENCE { [0] keyIdentifier, [1] issuer, [2] serial } */
static int ext_authority_key_id(nxt_x509_cert *c, der_reader v, int critical) {
    der_reader seq, id;
    int has_id;
    if (critical) return -1;    /* RFC 5280 4.2.1.1: MUST NOT be critical */
    if (der_expect(&v, DER_SEQUENCE, &seq, NULL) != 0 || !der_empty(&v) ||
        der_optional(&seq, DER_CTX(0), &id, &has_id) != 0)
        return -1;
    while (!der_empty(&seq)) {
        uint8_t tag;
        if (der_next(&seq, &tag, NULL, NULL) != 0 || (tag != DER_CTX_CONS(1) && tag != DER_CTX(2)))
            return -1;
        c->ext_present |= NXT_EXT_AKI_EXTRA;
    }
    if (has_id) c->authority_key_id = slice(&id);
    return 0;
}

/* AuthorityInfoAccess ::= SEQUENCE SIZE (1..MAX) OF SEQUENCE { OID, GeneralName } */
static int ext_authority_info(nxt_x509_cert *c, der_reader v, int critical) {
    der_reader seq;
    (void)c;
    if (critical || der_expect(&v, DER_SEQUENCE, &seq, NULL) != 0 || !der_empty(&v) ||
        der_empty(&seq))
        return -1;
    while (!der_empty(&seq)) {
        der_reader ad, oid, gn;
        uint8_t tag;
        if (der_expect(&seq, DER_SEQUENCE, &ad, NULL) != 0 ||
            der_expect(&ad, DER_OID, &oid, NULL) != 0 || oid.len == 0 ||
            der_next(&ad, &tag, NULL, &gn) != 0 || !der_empty(&ad) ||
            check_general_names(gn, 0) != 0)
            return -1;
    }
    return 0;
}

/* policyConstraints: must be critical (RFC 5280 4.2.1.11), and policy
 * processing is not implemented, so a critical one is refused too. */
static int ext_policy_constraints(nxt_x509_cert *c, der_reader v, int critical) {
    (void)c;
    (void)v;
    return critical ? -2 : -1;
}

static const struct {
    const uint8_t *oid;
    size_t len;
    uint32_t flag;
    int (*parse)(nxt_x509_cert *, der_reader, int);
} KNOWN_EXTS[] = {
    { OID_BASIC_CONSTRAINTS, 3, NXT_EXT_BASIC_CONSTRAINTS, ext_basic_constraints },
    { OID_KEY_USAGE, 3, NXT_EXT_KEY_USAGE, ext_key_usage },
    { OID_EXT_KEY_USAGE, 3, NXT_EXT_EXT_KEY_USAGE, ext_ext_key_usage },
    { OID_SUBJECT_ALT_NAME, 3, NXT_EXT_SUBJECT_ALT_NAME, ext_subject_alt_name },
    { OID_NAME_CONSTRAINTS, 3, NXT_EXT_NAME_CONSTRAINTS, ext_name_constraints },
    { OID_SUBJECT_KEY_ID, 3, NXT_EXT_SUBJECT_KEY_ID, ext_subject_key_id },
    { OID_AUTHORITY_KEY_ID, 3, NXT_EXT_AUTHORITY_KEY_ID, ext_authority_key_id },
    { OID_AUTHORITY_INFO, 8, NXT_EXT_AUTHORITY_INFO, ext_authority_info },
    { OID_POLICY_CONSTRAINTS, 3, 0, ext_policy_constraints },
};

static int parse_extensions(nxt_x509_cert *c, der_reader exts) {
    der_reader seq;
    if (der_expect(&exts, DER_SEQUENCE, &seq, NULL) != 0 || !der_empty(&exts) || der_empty(&seq))
        return NXT_X509_ERR_ENCODING;
    der_reader seen[MAX_EXTENSIONS];
    size_t n_seen = 0;
    while (!der_empty(&seq)) {
        der_reader ext, oid, value;
        int critical = 0;
        if (der_expect(&seq, DER_SEQUENCE, &ext, NULL) != 0 ||
            der_expect(&ext, DER_OID, &oid, NULL) != 0 || oid.len == 0)
            return NXT_X509_ERR_ENCODING;
        if (der_peek(&ext) == DER_BOOLEAN) {
            if (der_boolean(&ext, &critical) != 0 || !critical)  /* DER: default FALSE omitted */
                return NXT_X509_ERR_ENCODING;
        }
        if (der_expect(&ext, DER_OCTET_STRING, &value, NULL) != 0 || !der_empty(&ext))
            return NXT_X509_ERR_ENCODING;

        /* RFC 5280 4.2: each extension at most once. */
        if (n_seen == MAX_EXTENSIONS) return NXT_X509_ERR_ENCODING;
        for (size_t i = 0; i < n_seen; i++)
            if (der_equal(&seen[i], oid.p, oid.len)) return NXT_X509_ERR_DUPLICATE_EXT;
        seen[n_seen++] = oid;

        int known = 0;
        for (size_t i = 0; i < sizeof(KNOWN_EXTS) / sizeof(KNOWN_EXTS[0]); i++) {
            if (!der_equal(&oid, KNOWN_EXTS[i].oid, KNOWN_EXTS[i].len)) continue;
            known = 1;
            int prc = KNOWN_EXTS[i].parse(c, value, critical);
            if (prc == -2) return NXT_X509_ERR_CRITICAL_EXT;
            if (prc != 0) return NXT_X509_ERR_EXTENSION;
            c->ext_present |= KNOWN_EXTS[i].flag;
            if (critical) c->ext_critical |= KNOWN_EXTS[i].flag;
        }
        if (!known && critical) return NXT_X509_ERR_CRITICAL_EXT;
    }
    return NXT_X509_OK;
}

/* ------------------------------------------------------------------ */
/* Certificate                                                         */
/* ------------------------------------------------------------------ */

static int parse_cert(nxt_x509_cert *c, const uint8_t *der, size_t der_len) {
    der_reader all, cert, cert_whole, tbs, tbs_whole, outer_alg, inner_alg, sig_bits;
    unsigned unused;
    nxt_sig_alg_t inner = NXT_SIG_NONE;
    int rc;

    der_init(&all, der, der_len);
    if (der_expect(&all, DER_SEQUENCE, &cert, &cert_whole) != 0 || !der_empty(&all) ||
        der_expect(&cert, DER_SEQUENCE, &tbs, &tbs_whole) != 0)
        return NXT_X509_ERR_ENCODING;
    c->raw = slice(&cert_whole);
    c->tbs = slice(&tbs_whole);

    rc = parse_sig_alg(&cert, &c->sig_alg, &outer_alg);
    if (rc != NXT_X509_OK) return rc;
    if (der_bit_string(&cert, &sig_bits, &unused) != 0 || unused != 0 || !der_empty(&cert))
        return NXT_X509_ERR_ENCODING;
    c->signature = slice(&sig_bits);

    /* version [0] EXPLICIT INTEGER: only v3 (value 2) is accepted. */
    der_reader ver;
    uint32_t version;
    if (der_expect(&tbs, DER_CTX_CONS(0), &ver, NULL) != 0) return NXT_X509_ERR_VERSION;
    if (der_small_uint(&ver, 2, &version) != 0 || !der_empty(&ver)) return NXT_X509_ERR_ENCODING;
    if (version != 2) return NXT_X509_ERR_VERSION;

    /* serialNumber: non-negative, at most 20 octets (RFC 5280 4.1.2.2).
     * Zero is refused on the path, not here: some trusted roots use it. */
    der_reader serial, serial_mag;
    if (der_expect(&tbs, DER_INTEGER, &serial, NULL) != 0 ||
        der_integer(&serial, 0, &serial_mag) != 0 || serial_mag.len > 20)
        return NXT_X509_ERR_ENCODING;
    c->serial = slice(&serial_mag);

    rc = parse_sig_alg(&tbs, &inner, &inner_alg);
    if (rc != NXT_X509_OK) return rc;
    /* The signed and unsigned copies must match exactly (RFC 5280 4.1.1.2). */
    if (!der_equal(&inner_alg, outer_alg.p, outer_alg.len)) return NXT_X509_ERR_SIG_ALG;

    der_reader name, name_whole;
    if (der_expect(&tbs, DER_SEQUENCE, &name, &name_whole) != 0 || check_name(&name, NULL) != 0 ||
        der_empty(&name))  /* issuer must not be empty (4.1.2.4) */
        return NXT_X509_ERR_ENCODING;
    c->issuer = slice(&name_whole);

    der_reader validity;
    if (der_expect(&tbs, DER_SEQUENCE, &validity, NULL) != 0) return NXT_X509_ERR_ENCODING;
    if (parse_time(&validity, &c->not_before) != 0 || parse_time(&validity, &c->not_after) != 0 ||
        !der_empty(&validity))
        return NXT_X509_ERR_TIME;

    if (der_expect(&tbs, DER_SEQUENCE, &name, &name_whole) != 0 || check_name(&name, c) != 0)
        return NXT_X509_ERR_ENCODING;
    c->subject = slice(&name_whole);

    rc = parse_spki(c, &tbs);
    if (rc != NXT_X509_OK) return rc;

    /* issuerUniqueID [1] and subjectUniqueID [2]: allowed, not used. */
    der_reader skip;
    int present;
    if (der_optional(&tbs, DER_CTX(1), &skip, &present) != 0 ||
        der_optional(&tbs, DER_CTX(2), &skip, &present) != 0)
        return NXT_X509_ERR_ENCODING;

    der_reader exts;
    if (der_optional(&tbs, DER_CTX_CONS(3), &exts, &present) != 0 || !der_empty(&tbs))
        return NXT_X509_ERR_ENCODING;
    if (present) {
        rc = parse_extensions(c, exts);
        if (rc != NXT_X509_OK) return rc;
    }
    /* An empty subject is allowed only with a critical subjectAltName
     * (RFC 5280 4.2.1.6). */
    if (c->subject.len == 2 && !(c->ext_critical & NXT_EXT_SUBJECT_ALT_NAME))
        return NXT_X509_ERR_EXTENSION;
    return NXT_X509_OK;
}

int nxt_x509_parse(nxt_x509_cert *cert, const uint8_t *der, size_t der_len) {
    if (cert == NULL) return NXT_X509_ERR_ARG;
    zero_cert(cert);
    if (der == NULL || der_len == 0) return NXT_X509_ERR_ARG;
    int rc = parse_cert(cert, der, der_len);
    if (rc != NXT_X509_OK) zero_cert(cert);
    return rc;
}

/* ------------------------------------------------------------------ */
/* Signature check                                                     */
/* ------------------------------------------------------------------ */

int nxt_x509_check_signature(const nxt_x509_cert *cert, const nxt_x509_cert *issuer) {
    if (cert == NULL || issuer == NULL || cert->tbs.p == NULL || issuer->key.p == NULL)
        return NXT_X509_ERR_ARG;
    nxc_hash_alg_t h = NXC_HASH_NONE;
    nxt_key_type_t need = NXT_KEY_RSA;
    int pss = 0;
    switch (cert->sig_alg) {
    case NXT_SIG_RSA_PKCS1_SHA256: h = NXC_HASH_SHA256; break;
    case NXT_SIG_RSA_PKCS1_SHA384: h = NXC_HASH_SHA384; break;
    case NXT_SIG_RSA_PKCS1_SHA512: h = NXC_HASH_SHA512; break;
    case NXT_SIG_RSA_PSS_SHA256: h = NXC_HASH_SHA256; pss = 1; break;
    case NXT_SIG_RSA_PSS_SHA384: h = NXC_HASH_SHA384; pss = 1; break;
    case NXT_SIG_RSA_PSS_SHA512: h = NXC_HASH_SHA512; pss = 1; break;
    case NXT_SIG_ECDSA_SHA256: h = NXC_HASH_SHA256; need = NXT_KEY_NONE; break;
    case NXT_SIG_ECDSA_SHA384: h = NXC_HASH_SHA384; need = NXT_KEY_NONE; break;
    case NXT_SIG_ECDSA_SHA512: h = NXC_HASH_SHA512; need = NXT_KEY_NONE; break;
    case NXT_SIG_ED25519: need = NXT_KEY_ED25519; break;
    default: return NXT_X509_ERR_SIG_ALG;
    }

    int ok;
    if (need == NXT_KEY_ED25519) {
        if (issuer->key_type != NXT_KEY_ED25519) return NXT_X509_ERR_SIG_ALG;
        if (cert->signature.len != NXC_ED25519_SIGNATURE_LEN) return NXT_X509_ERR_BAD_SIGNATURE;
        ok = nxc_ed25519_verify(cert->signature.p, cert->tbs.p, cert->tbs.len, issuer->key.p) == NXC_OK;
        return ok ? NXT_X509_OK : NXT_X509_ERR_BAD_SIGNATURE;
    }

    uint8_t digest[NXC_HASH_MAX_DIGEST_LEN];
    size_t dlen = nxc_hash_digest_len(h);
    if (nxc_hash(h, cert->tbs.p, cert->tbs.len, digest, sizeof(digest)) != NXC_OK)
        return NXT_X509_ERR_ARG;
    if (need == NXT_KEY_RSA) {
        if (issuer->key_type != NXT_KEY_RSA) return NXT_X509_ERR_SIG_ALG;
        ok = (pss ? nxc_rsa_verify_pss(issuer->key.p, issuer->key.len, issuer->rsa_e.p,
                                       issuer->rsa_e.len, h, digest, dlen, dlen,
                                       cert->signature.p, cert->signature.len)
                  : nxc_rsa_verify_pkcs1v15(issuer->key.p, issuer->key.len, issuer->rsa_e.p,
                                            issuer->rsa_e.len, h, digest, dlen,
                                            cert->signature.p, cert->signature.len)) == NXC_OK;
    } else {
        nxc_ec_curve_t curve;
        if (issuer->key_type == NXT_KEY_EC_P256) curve = NXC_EC_P256;
        else if (issuer->key_type == NXT_KEY_EC_P384) curve = NXC_EC_P384;
        else return NXT_X509_ERR_SIG_ALG;
        ok = nxc_ecdsa_verify_der(curve, issuer->key.p, issuer->key.len, digest, dlen,
                                  cert->signature.p, cert->signature.len) == NXC_OK;
    }
    return ok ? NXT_X509_OK : NXT_X509_ERR_BAD_SIGNATURE;
}

const char *nxt_x509_err_str(int err) {
    switch (err) {
    case NXT_X509_OK: return "ok";
    case NXT_X509_ERR_ARG: return "bad argument";
    case NXT_X509_ERR_ENCODING: return "malformed certificate";
    case NXT_X509_ERR_VERSION: return "not an X.509 v3 certificate";
    case NXT_X509_ERR_TIME: return "malformed validity time";
    case NXT_X509_ERR_KEY: return "unsupported or malformed public key";
    case NXT_X509_ERR_WEAK_KEY: return "RSA key shorter than 2048 bits";
    case NXT_X509_ERR_SIG_ALG: return "unsupported signature algorithm";
    case NXT_X509_ERR_CRITICAL_EXT: return "unknown critical extension";
    case NXT_X509_ERR_DUPLICATE_EXT: return "duplicate extension";
    case NXT_X509_ERR_EXTENSION: return "malformed extension";
    case NXT_X509_ERR_BAD_SIGNATURE: return "bad signature";
    case NXT_X509_ERR_EXPIRED: return "certificate expired";
    case NXT_X509_ERR_NOT_YET_VALID: return "certificate not yet valid";
    case NXT_X509_ERR_UNKNOWN_ISSUER: return "unknown issuer (not signed by a trusted root)";
    case NXT_X509_ERR_NOT_CA: return "issuer is not a CA";
    case NXT_X509_ERR_PATH_LEN: return "path length constraint exceeded";
    case NXT_X509_ERR_KEY_USAGE: return "key usage does not allow this use";
    case NXT_X509_ERR_EKU: return "not valid for TLS server authentication";
    case NXT_X509_ERR_NAME_CONSTRAINTS: return "name outside the issuer's name constraints";
    case NXT_X509_ERR_HOST_MISMATCH: return "certificate is not valid for this host name";
    case NXT_X509_ERR_LEAF_IS_CA: return "server certificate is a CA certificate";
    case NXT_X509_ERR_CHAIN_TOO_LONG: return "certificate chain too long";
    case NXT_X509_ERR_BUDGET: return "too much work building the certificate path";
    case NXT_X509_ERR_BAD_HOST: return "invalid host name";
    case NXT_X509_ERR_PROFILE: return "certificate breaks web PKI rules";
    default: return "unknown error";
    }
}
