/*
 * nxserver - window protocol message checks (see nxw_check.h)
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include "nxw_check.h"

static const char *const g_reasons[NXW_V_COUNT] = {
    "ok",
    "shorter than the header",
    "header length differs from the bytes received",
    "unknown op",
    "length does not match the op",
    "wrong number of handles",
    "message before HELLO",
    "handle is not mappable shared memory",
};

const char *nxw_violation_str(int v) {
    if (v < 0 || v >= NXW_V_COUNT) return "unknown violation";
    return g_reasons[v];
}

uint32_t nxw_request_size(uint16_t op) {
    switch (op) {
        case NXW_OP_HELLO:         return (uint32_t)sizeof(nxw_hello_t);
        case NXW_OP_WIN_CREATE:    return (uint32_t)sizeof(nxw_win_create_t);
        case NXW_OP_WIN_DESTROY:   return (uint32_t)sizeof(nxw_win_destroy_t);
        case NXW_OP_WIN_SET_TITLE: return (uint32_t)sizeof(nxw_win_set_title_t);
        case NXW_OP_BUF_ATTACH:    return (uint32_t)sizeof(nxw_buf_attach_t);
        case NXW_OP_COMMIT:        return (uint32_t)sizeof(nxw_commit_t);
        default:                   return 0;
    }
}

int nxw_check_frame(const void *data, uint32_t data_len, uint32_t handle_count,
                    int hello_done, uint16_t *op_out) {
    if (!data || data_len < (uint32_t)sizeof(nxw_hdr_t)) return NXW_V_SHORT;
    const nxw_hdr_t *h = (const nxw_hdr_t *)data;
    if ((uint32_t)h->len != data_len) return NXW_V_LEN;
    uint32_t need = nxw_request_size(h->op);
    if (need == 0) return NXW_V_OP;
    if (data_len != need) return NXW_V_SIZE;
    uint32_t handles = (h->op == NXW_OP_BUF_ATTACH) ? 1u : 0u;
    if (handle_count != handles) return NXW_V_HANDLES;
    if (!hello_done && h->op != NXW_OP_HELLO) return NXW_V_BEFORE_HELLO;
    if (op_out) *op_out = h->op;
    return NXW_FRAME_OK;
}

int nxw_check_size(uint32_t width, uint32_t height) {
    if (width == 0 || height == 0 || width > NXW_SIZE_MAX || height > NXW_SIZE_MAX) {
        return -NXW_EINVAL;
    }
    return 0;
}

int nxw_check_buffer(uint32_t width, uint32_t height, uint32_t stride,
                     uint64_t offset, uint64_t object_size) {
    if (nxw_check_size(width, height) != 0) return -NXW_EINVAL;
    if ((stride & 3u) != 0 || stride > NXW_STRIDE_MAX) return -NXW_EINVAL;
    if ((uint64_t)stride < (uint64_t)width * 4u) return -NXW_EINVAL;
    if ((offset & 3u) != 0) return -NXW_EINVAL;                 /* pixels are 32-bit */
    /* stride <= 16384 and height <= 4096: the product fits easily in 64 bits */
    uint64_t bytes = (uint64_t)stride * (uint64_t)height;
    if (offset > object_size || bytes > object_size - offset) return -NXW_EINVAL;
    return 0;
}

/* Length of the valid UTF-8 sequence at s (at most n bytes), 0 if invalid. */
static uint32_t utf8_seq_len(const uint8_t *s, uint32_t n) {
    uint8_t c = s[0];
    uint32_t len;
    uint32_t cp;
    if (c < 0x80u) return 1;
    if ((c & 0xE0u) == 0xC0u) { len = 2; cp = c & 0x1Fu; }
    else if ((c & 0xF0u) == 0xE0u) { len = 3; cp = c & 0x0Fu; }
    else if ((c & 0xF8u) == 0xF0u) { len = 4; cp = c & 0x07u; }
    else return 0;
    if (len > n) return 0;
    for (uint32_t i = 1; i < len; i++) {
        if ((s[i] & 0xC0u) != 0x80u) return 0;
        cp = (cp << 6) | (s[i] & 0x3Fu);
    }
    /* Reject overlong forms, surrogates and values past U+10FFFF */
    if ((len == 2 && cp < 0x80u) || (len == 3 && cp < 0x800u) || (len == 4 && cp < 0x10000u)) return 0;
    if (cp >= 0xD800u && cp <= 0xDFFFu) return 0;
    if (cp > 0x10FFFFu) return 0;
    return len;
}

int nxw_title_sanitize(const char in[NXW_TITLE_MAX], char out[NXW_TITLE_MAX]) {
    if (!in || !out) return -NXW_EINVAL;
    uint32_t n = 0;
    while (n < NXW_TITLE_MAX && in[n] != '\0') n++;
    if (n >= NXW_TITLE_MAX) return -NXW_EINVAL;     /* no NUL inside the field */

    const uint8_t *s = (const uint8_t *)in;
    uint32_t i = 0, o = 0;
    while (i < n) {
        uint32_t len = utf8_seq_len(s + i, n - i);
        if (len == 0 || (len == 1 && (s[i] < 0x20u || s[i] == 0x7Fu))) {
            out[o++] = '?';
            i++;
            continue;
        }
        for (uint32_t k = 0; k < len; k++) out[o++] = (char)s[i + k];
        i += len;
    }
    out[o] = '\0';   /* o <= n < NXW_TITLE_MAX: replacements never grow the text */
    return 0;
}

int nxw_clip_rect(const nxw_rect_t *r, uint32_t bw, uint32_t bh, nxw_rect_t *out) {
    if (!r || !out) return 0;
    int64_t x0 = r->x, y0 = r->y;
    int64_t x1 = x0 + (int64_t)r->w, y1 = y0 + (int64_t)r->h;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > (int64_t)bw) x1 = (int64_t)bw;
    if (y1 > (int64_t)bh) y1 = (int64_t)bh;
    if (x1 <= x0 || y1 <= y0) return 0;
    out->x = (int32_t)x0;
    out->y = (int32_t)y0;
    out->w = (uint32_t)(x1 - x0);
    out->h = (uint32_t)(y1 - y0);
    return 1;
}
