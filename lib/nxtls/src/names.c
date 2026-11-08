/*
 * nxtls - DNS name and IP address syntax shared by the X.509 code
 * (internal; see names.h).
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include "names.h"

uint8_t nxt__lower(uint8_t c) { return (c >= 'A' && c <= 'Z') ? (uint8_t)(c + 32) : c; }

int nxt__ieq(const uint8_t *a, const uint8_t *b, size_t n) {
    for (size_t i = 0; i < n; i++)
        if (nxt__lower(a[i]) != nxt__lower(b[i])) return 0;
    return 1;
}

size_t nxt__strlen(const char *s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

/*
 * A DNS name as used for matching: labels of letters, digits and '-'
 * (RFC 1034 3.5 preferred syntax; '_' is not allowed in certificates,
 * CA/Browser Forum BR 7.1.2.7.12), 1..63 bytes each, 253 bytes at most, no empty labels. With
 * allow_wildcard, the first label may be exactly "*" if at least two
 * labels follow.
 */
int nxt__valid_dns(const uint8_t *s, size_t n, int allow_wildcard) {
    if (n == 0 || n > 253) return 0;
    size_t label = 0, labels = 1;
    int wildcard = allow_wildcard && n >= 2 && s[0] == '*' && s[1] == '.';
    for (size_t i = wildcard ? 2 : 0; i < n; i++) {
        uint8_t c = s[i];
        if (c == '.') {
            if (label == 0) return 0;
            label = 0;
            labels++;
            continue;
        }
        int ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                 c == '-';
        if (!ok || ++label > 63) return 0;
    }
    if (label == 0) return 0;                  /* empty last label */
    if (wildcard && labels < 2) return 0;      /* "*.com" is not allowed */
    return 1;
}

/* Strict dotted-quad: four decimal numbers 0..255, no leading zeros. */
int nxt__parse_ipv4(const uint8_t *s, size_t n, uint8_t out[4]) {
    size_t i = 0;
    for (int part = 0; part < 4; part++) {
        if (part > 0) {
            if (i >= n || s[i] != '.') return 0;
            i++;
        }
        size_t start = i;
        unsigned v = 0;
        while (i < n && s[i] >= '0' && s[i] <= '9' && i - start < 3) v = v * 10 + (unsigned)(s[i++] - '0');
        if (i == start || v > 255 || (i - start > 1 && s[start] == '0')) return 0;
        out[part] = (uint8_t)v;
    }
    return i == n;
}

static int hexdig(uint8_t c) {
    if (c >= '0' && c <= '9') return c - '0';
    c = nxt__lower(c);
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

/* RFC 4291 2.2 text form: hex groups, one "::", optional trailing IPv4. */
int nxt__parse_ipv6(const uint8_t *s, size_t n, uint8_t out[16]) {
    uint8_t words[16];
    int count = 0, gap = -1;
    size_t i = 0;
    if (n < 2) return 0;
    if (s[0] == ':') {
        if (s[1] != ':') return 0;
        gap = 0;
        i = 2;
    }
    while (i < n) {
        if (count == 8) return 0;
        /* trailing IPv4 */
        size_t j = i;
        while (j < n && s[j] != ':' && s[j] != '.') j++;
        if (j < n && s[j] == '.') {
            if (count > 6) return 0;
            if (!nxt__parse_ipv4(s + i, n - i, words + count * 2)) return 0;
            count += 2;
            i = n;
            break;
        }
        int v = 0, digits = 0;
        while (i < n && s[i] != ':') {
            int h = hexdig(s[i]);
            if (h < 0 || ++digits > 4) return 0;
            v = v << 4 | h;
            i++;
        }
        if (digits == 0) return 0;
        words[count * 2] = (uint8_t)(v >> 8);
        words[count * 2 + 1] = (uint8_t)v;
        count++;
        if (i == n) break;
        i++;                                    /* the ':' */
        if (i < n && s[i] == ':') {
            if (gap >= 0) return 0;             /* only one "::" */
            gap = count;
            i++;
        } else if (i == n) {
            return 0;                           /* trailing single ':' */
        }
    }
    if (gap < 0 && count != 8) return 0;
    if (gap >= 0 && count >= 8) return 0;       /* "::" must stand for at least one group */
    int fill = 8 - count;
    for (int k = 0, w = 0; k < 8; k++) {
        if (gap >= 0 && k >= gap && k < gap + fill) {
            out[k * 2] = 0;
            out[k * 2 + 1] = 0;
        } else {
            out[k * 2] = words[w * 2];
            out[k * 2 + 1] = words[w * 2 + 1];
            w++;
        }
    }
    return 1;
}

/* RFC 5952 section 4 text form of a 16-byte IPv6 address. */
static size_t ipv6_text(const uint8_t ip[16], char *out) {
    static const char hex[] = "0123456789abcdef";
    int best = -1, best_len = 0;
    for (int i = 0; i < 8;) {                  /* longest run (2+) of zero groups, leftmost */
        int j = i;
        while (j < 8 && ip[j * 2] == 0 && ip[j * 2 + 1] == 0) j++;
        if (j - i > best_len && j - i >= 2) {
            best = i;
            best_len = j - i;
        }
        i = j > i ? j : i + 1;
    }
    size_t n = 0;
    for (int i = 0; i < 8; i++) {
        if (i == best) {
            out[n++] = ':';
            if (i == 0) out[n++] = ':';
            i += best_len - 1;
            continue;
        }
        unsigned w = (unsigned)ip[i * 2] << 8 | ip[i * 2 + 1];
        int started = 0;
        for (int sh = 12; sh >= 0; sh -= 4) {
            unsigned d = (w >> sh) & 0xf;
            if (d || started || sh == 0) {
                out[n++] = hex[d];
                started = 1;
            }
        }
        if (i < 7) out[n++] = ':';
    }
    return n;
}

size_t nxt__ip_text(const uint8_t *ip, size_t len, char out[NXT_IP_TEXT_MAX]) {
    if (len == 16) return ipv6_text(ip, out);
    size_t n = 0;
    for (size_t i = 0; i < 4; i++) {
        unsigned v = ip[i];
        if (i) out[n++] = '.';
        if (v >= 100) out[n++] = (char)('0' + v / 100);
        if (v >= 10) out[n++] = (char)('0' + v / 10 % 10);
        out[n++] = (char)('0' + v % 10);
    }
    return n;
}
