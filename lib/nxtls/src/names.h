/*
 * nxtls - DNS name and IP address syntax (internal).
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#ifndef NXTLS_NAMES_H
#define NXTLS_NAMES_H

#include <stddef.h>
#include <stdint.h>

#define NXT_IP_TEXT_MAX 40      /* longest RFC 5952 IPv6 text, no terminator */

uint8_t nxt__lower(uint8_t c);
/* ASCII case-insensitive comparison of n bytes; 1 if equal. */
int nxt__ieq(const uint8_t *a, const uint8_t *b, size_t n);
size_t nxt__strlen(const char *s);
/* 1 if s[0..n) is a valid DNS name; with allow_wildcard, "*.a.b" too. */
int nxt__valid_dns(const uint8_t *s, size_t n, int allow_wildcard);
/* Strict text forms; 1 on success. */
int nxt__parse_ipv4(const uint8_t *s, size_t n, uint8_t out[4]);
int nxt__parse_ipv6(const uint8_t *s, size_t n, uint8_t out[16]);
/* Canonical text (dotted quad, or RFC 5952 for IPv6); returns its length. */
size_t nxt__ip_text(const uint8_t *ip, size_t len, char out[NXT_IP_TEXT_MAX]);

#endif /* NXTLS_NAMES_H */
