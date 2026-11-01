/*
 * Minimal JSON reader for the nxcrypto host tests (Wycheproof vector
 * files). Host-only; uses the C library.
 *
 * Accepts RFC 8259 JSON: objects, arrays, strings, numbers, true, false,
 * null. String escapes are decoded; \u escapes outside ASCII are stored
 * as '?', which is enough for the comment fields Wycheproof uses (every
 * value the tests read is ASCII hex or a plain name). Nesting is limited
 * to JSON_MAX_DEPTH. Any syntax error ends the test with status 2, since
 * a vector file that cannot be read must never count as a pass.
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#ifndef CRYPTO_JSON_H
#define CRYPTO_JSON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define JSON_MAX_DEPTH 32

typedef enum { JSON_NULL, JSON_BOOL, JSON_NUM, JSON_STR, JSON_ARR, JSON_OBJ } json_type;

typedef struct json json;
struct json {
    json_type type;
    char *str;              /* JSON_STR */
    double num;             /* JSON_NUM */
    int boolean;            /* JSON_BOOL */
    size_t len;             /* JSON_ARR / JSON_OBJ: number of items */
    char **keys;            /* JSON_OBJ */
    json **items;           /* JSON_ARR / JSON_OBJ */
};

typedef struct {
    const char *p;
    const char *name;       /* file name for error messages */
} json_in;

static void json_die(json_in *in, const char *what) {
    printf("ERROR: %s: JSON %s near \"%.20s\"\n", in->name, what, in->p);
    exit(2);
}

static void *json_alloc(size_t n) {
    void *p = calloc(1, n ? n : 1);
    if (!p) { perror("calloc"); exit(2); }
    return p;
}

static void json_ws(json_in *in) {
    while (*in->p == ' ' || *in->p == '\t' || *in->p == '\n' || *in->p == '\r') in->p++;
}

static char *json_parse_str(json_in *in) {
    if (*in->p != '"') json_die(in, "expected string");
    in->p++;
    const char *start = in->p;
    while (*in->p && *in->p != '"') {
        if (*in->p == '\\' && in->p[1]) in->p++;
        in->p++;
    }
    if (*in->p != '"') json_die(in, "unterminated string");
    size_t raw = (size_t)(in->p - start);
    char *out = json_alloc(raw + 1), *o = out;
    for (const char *s = start; s < in->p; s++) {
        if ((unsigned char)*s < 0x20) json_die(in, "control character in string");
        if (*s != '\\') { *o++ = *s; continue; }
        s++;
        switch (*s) {
        case '"': *o++ = '"'; break;
        case '\\': *o++ = '\\'; break;
        case '/': *o++ = '/'; break;
        case 'b': *o++ = '\b'; break;
        case 'f': *o++ = '\f'; break;
        case 'n': *o++ = '\n'; break;
        case 'r': *o++ = '\r'; break;
        case 't': *o++ = '\t'; break;
        case 'u': {
            unsigned v = 0;
            for (int i = 1; i <= 4; i++) {
                int h = hexval(s[i]);
                if (h < 0) json_die(in, "bad \\u escape");
                v = v << 4 | (unsigned)h;
            }
            *o++ = v < 0x80 ? (char)v : '?';
            s += 4;
            break;
        }
        default: json_die(in, "bad escape");
        }
    }
    *o = 0;
    in->p++;
    return out;
}

static json *json_value(json_in *in, int depth);

static void json_push(json *v, char *key, json *item) {
    v->items = realloc(v->items, (v->len + 1) * sizeof(*v->items));
    if (!v->items) { perror("realloc"); exit(2); }
    if (v->type == JSON_OBJ) {
        v->keys = realloc(v->keys, (v->len + 1) * sizeof(*v->keys));
        if (!v->keys) { perror("realloc"); exit(2); }
        v->keys[v->len] = key;
    }
    v->items[v->len++] = item;
}

static json *json_value(json_in *in, int depth) {
    if (depth > JSON_MAX_DEPTH) json_die(in, "nested too deeply");
    json_ws(in);
    json *v = json_alloc(sizeof(*v));
    char c = *in->p;
    if (c == '{' || c == '[') {
        v->type = c == '{' ? JSON_OBJ : JSON_ARR;
        char close = c == '{' ? '}' : ']';
        in->p++;
        json_ws(in);
        if (*in->p == close) { in->p++; return v; }
        for (;;) {
            char *key = NULL;
            if (v->type == JSON_OBJ) {
                json_ws(in);
                key = json_parse_str(in);
                json_ws(in);
                if (*in->p != ':') json_die(in, "expected ':'");
                in->p++;
            }
            json_push(v, key, json_value(in, depth + 1));
            json_ws(in);
            if (*in->p == ',') { in->p++; continue; }
            if (*in->p == close) { in->p++; return v; }
            json_die(in, "expected ',' or closing bracket");
        }
    }
    if (c == '"') { v->type = JSON_STR; v->str = json_parse_str(in); return v; }
    if (!strncmp(in->p, "true", 4)) { v->type = JSON_BOOL; v->boolean = 1; in->p += 4; return v; }
    if (!strncmp(in->p, "false", 5)) { v->type = JSON_BOOL; in->p += 5; return v; }
    if (!strncmp(in->p, "null", 4)) { v->type = JSON_NULL; in->p += 4; return v; }
    if (c == '-' || (c >= '0' && c <= '9')) {
        char *end;
        v->type = JSON_NUM;
        v->num = strtod(in->p, &end);
        if (end == in->p) json_die(in, "bad number");
        in->p = end;
        return v;
    }
    json_die(in, "unexpected character");
    return NULL;
}

/* Parse a whole document; trailing non-space text is an error. */
static json *json_parse(const char *text, const char *name) {
    json_in in = { text, name };
    json *v = json_value(&in, 0);
    json_ws(&in);
    if (*in.p) json_die(&in, "trailing data");
    return v;
}

static void json_free(json *v) {
    if (!v) return;
    for (size_t i = 0; i < v->len; i++) {
        json_free(v->items[i]);
        if (v->keys) free(v->keys[i]);
    }
    free(v->items);
    free(v->keys);
    free(v->str);
    free(v);
}

/* Member of an object, or NULL. */
static json *json_get(const json *obj, const char *key) {
    if (!obj || obj->type != JSON_OBJ) return NULL;
    for (size_t i = 0; i < obj->len; i++)
        if (!strcmp(obj->keys[i], key)) return obj->items[i];
    return NULL;
}

/* String member, or NULL if missing or not a string. */
static const char *json_str(const json *obj, const char *key) {
    json *v = json_get(obj, key);
    return v && v->type == JSON_STR ? v->str : NULL;
}

/* Numeric member, or dflt if missing or not a number. */
static long json_long(const json *obj, const char *key, long dflt) {
    json *v = json_get(obj, key);
    return v && v->type == JSON_NUM ? (long)v->num : dflt;
}

#endif /* CRYPTO_JSON_H */
