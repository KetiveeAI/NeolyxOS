/*
 * Host unit test for the window protocol checks
 * (desktop/shell/nxserver/nxw_check.c).
 *
 * Covers every framing violation that must close a connection, the size,
 * buffer and title rules that give a recoverable ERROR, and damage clipping.
 *
 * Build and run on the development machine:
 *   gcc -std=c11 -O1 -Wall -Wextra -o /tmp/nxw_check_test nxw_check_test.c
 *   /tmp/nxw_check_test
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "../../desktop/shell/nxserver/nxw_check.c"

static int g_fail, g_checks;

static void check(int ok, const char *what) {
    g_checks++;
    if (!ok) {
        printf("FAIL %s\n", what);
        g_fail = 1;
    }
}

static void hdr(void *m, uint16_t op, uint16_t len) {
    nxw_hdr_t *h = (nxw_hdr_t *)m;
    h->op = op;
    h->len = len;
    h->seq = 7;
}

int main(void) {
    uint8_t buf[1024];
    uint16_t op = 0;

    /* ---- Framing ---- */
    memset(buf, 0, sizeof(buf));
    hdr(buf, NXW_OP_HELLO, sizeof(nxw_hello_t));
    check(nxw_check_frame(buf, sizeof(nxw_hello_t), 0, 0, &op) == NXW_FRAME_OK && op == NXW_OP_HELLO,
          "HELLO first is accepted");
    check(nxw_check_frame(buf, 4, 0, 0, &op) == NXW_V_SHORT, "4 bytes: shorter than the header");
    check(nxw_check_frame(NULL, 0, 0, 0, &op) == NXW_V_SHORT, "no data: shorter than the header");
    check(nxw_check_frame(buf, sizeof(nxw_hello_t) + 8, 0, 0, &op) == NXW_V_LEN,
          "header length differs from the bytes received");
    hdr(buf, NXW_OP_HELLO, 1024);
    check(nxw_check_frame(buf, 1024, 0, 0, &op) == NXW_V_SIZE, "HELLO padded to 1024 bytes: larger than its struct");
    hdr(buf, NXW_OP_HELLO, 8);
    check(nxw_check_frame(buf, 8, 0, 0, &op) == NXW_V_SIZE, "HELLO without a body");
    hdr(buf, 0x01FF, 16);
    check(nxw_check_frame(buf, 16, 0, 1, &op) == NXW_V_OP, "unknown op");
    hdr(buf, NXW_OP_WELCOME, sizeof(nxw_welcome_t));
    check(nxw_check_frame(buf, sizeof(nxw_welcome_t), 0, 1, &op) == NXW_V_OP, "event op sent to the server");
    hdr(buf, NXW_OP_WIN_CREATE, sizeof(nxw_win_create_t));
    check(nxw_check_frame(buf, sizeof(nxw_win_create_t), 0, 0, &op) == NXW_V_BEFORE_HELLO,
          "WIN_CREATE before HELLO");
    check(nxw_check_frame(buf, sizeof(nxw_win_create_t), 0, 1, &op) == NXW_FRAME_OK, "WIN_CREATE after HELLO");
    hdr(buf, NXW_OP_HELLO, sizeof(nxw_hello_t));
    check(nxw_check_frame(buf, sizeof(nxw_hello_t), 1, 0, &op) == NXW_V_HANDLES, "HELLO with a handle");
    hdr(buf, NXW_OP_BUF_ATTACH, sizeof(nxw_buf_attach_t));
    check(nxw_check_frame(buf, sizeof(nxw_buf_attach_t), 0, 1, &op) == NXW_V_HANDLES, "BUF_ATTACH without a handle");
    check(nxw_check_frame(buf, sizeof(nxw_buf_attach_t), 2, 1, &op) == NXW_V_HANDLES, "BUF_ATTACH with two handles");
    check(nxw_check_frame(buf, sizeof(nxw_buf_attach_t), 1, 1, &op) == NXW_FRAME_OK, "BUF_ATTACH with one handle");
    hdr(buf, NXW_OP_COMMIT, sizeof(nxw_commit_t));
    check(nxw_check_frame(buf, sizeof(nxw_commit_t), 0, 1, &op) == NXW_FRAME_OK && op == NXW_OP_COMMIT,
          "COMMIT at its full size");
    hdr(buf, NXW_OP_COMMIT, 16);
    check(nxw_check_frame(buf, 16, 0, 1, &op) == NXW_V_SIZE, "COMMIT cut short");
    check(nxw_request_size(NXW_OP_ERROR) == 0 && nxw_request_size(NXW_OP_WIN_DESTROY) == 12,
          "request sizes");
    int all_named = 1;
    for (int v = 0; v < NXW_V_COUNT; v++) if (!nxw_violation_str(v)[0]) all_named = 0;
    check(all_named && strcmp(nxw_violation_str(99), "unknown violation") == 0, "every violation has a reason");

    /* ---- Sizes ---- */
    check(nxw_check_size(1, 1) == 0 && nxw_check_size(4096, 4096) == 0, "sizes 1 and 4096 accepted");
    check(nxw_check_size(0, 10) == -NXW_EINVAL && nxw_check_size(10, 0) == -NXW_EINVAL, "size 0 refused");
    check(nxw_check_size(4097, 10) == -NXW_EINVAL && nxw_check_size(10, 5000) == -NXW_EINVAL,
          "size above 4096 refused");

    /* ---- Buffers ---- */
    uint64_t one_mib = 1u << 20;
    check(nxw_check_buffer(320, 240, 1280, 0, one_mib) == 0, "320x240 buffer in 1 MiB");
    check(nxw_check_buffer(320, 240, 1276, 0, one_mib) == -NXW_EINVAL, "stride below width * 4");
    check(nxw_check_buffer(320, 240, 1282, 0, one_mib) == -NXW_EINVAL, "stride not a multiple of 4");
    check(nxw_check_buffer(4096, 1, 16388, 0, one_mib) == -NXW_EINVAL, "stride above 16384");
    check(nxw_check_buffer(4096, 4096, 16384, 0, 64ull << 20) == 0, "4096x4096 fills a 64 MiB object exactly");
    check(nxw_check_buffer(4096, 4096, 16384, 4, 64ull << 20) == -NXW_EINVAL, "one pixel past the object");
    check(nxw_check_buffer(320, 240, 1280, one_mib, one_mib) == -NXW_EINVAL, "offset at the end of the object");
    check(nxw_check_buffer(320, 240, 1280, UINT64_MAX - 3, one_mib) == -NXW_EINVAL, "offset near 2^64 (no overflow)");
    check(nxw_check_buffer(320, 240, 1280, 2, one_mib) == -NXW_EINVAL, "offset not 4-byte aligned");
    check(nxw_check_buffer(320, 240, 1280, one_mib - 1280u * 240u, one_mib) == 0, "buffer at the end of the object");
    check(nxw_check_buffer(0, 240, 1280, 0, one_mib) == -NXW_EINVAL, "buffer width 0");

    /* ---- Titles ---- */
    char in[NXW_TITLE_MAX], out[NXW_TITLE_MAX];
    memset(in, 0, sizeof(in));
    strcpy(in, "Hello");
    check(nxw_title_sanitize(in, out) == 0 && strcmp(out, "Hello") == 0, "plain title");
    strcpy(in, "Caf\xC3\xA9 \xE2\x82\xAC \xF0\x9F\x98\x80");
    check(nxw_title_sanitize(in, out) == 0 && strcmp(out, in) == 0, "valid 2, 3 and 4 byte UTF-8 kept");
    strcpy(in, "a\xFF" "b\xC3" "c\x01" "d\xC0\xAF" "e\xED\xA0\x80");
    check(nxw_title_sanitize(in, out) == 0 && strcmp(out, "a?b?c?d??e???") == 0,
          "invalid bytes, control, overlong and surrogate replaced");
    memset(in, 'A', sizeof(in));
    memset(out, 'x', sizeof(out));
    check(nxw_title_sanitize(in, out) == -NXW_EINVAL && out[0] == 'x', "title without a NUL refused");
    memset(in, 'A', sizeof(in));
    in[NXW_TITLE_MAX - 1] = '\0';
    check(nxw_title_sanitize(in, out) == 0 && strlen(out) == NXW_TITLE_MAX - 1, "63-byte title accepted");
    in[0] = '\xF0'; in[1] = '\x9F'; in[2] = '\0';
    check(nxw_title_sanitize(in, out) == 0 && strcmp(out, "??") == 0, "truncated sequence replaced");

    /* ---- Damage clipping ---- */
    nxw_rect_t r, o;
    r.x = -10; r.y = -10; r.w = 20; r.h = 20;
    check(nxw_clip_rect(&r, 100, 100, &o) == 1 && o.x == 0 && o.y == 0 && o.w == 10 && o.h == 10,
          "rect clipped at the top-left");
    r.x = 90; r.y = 95; r.w = 50; r.h = 50;
    check(nxw_clip_rect(&r, 100, 100, &o) == 1 && o.w == 10 && o.h == 5, "rect clipped at the bottom-right");
    r.x = 100; r.y = 0; r.w = 5; r.h = 5;
    check(nxw_clip_rect(&r, 100, 100, &o) == 0, "rect outside the buffer is empty");
    r.x = INT32_MAX; r.y = INT32_MAX; r.w = UINT32_MAX; r.h = UINT32_MAX;
    check(nxw_clip_rect(&r, 100, 100, &o) == 0, "huge rect does not overflow");
    r.x = INT32_MIN; r.y = INT32_MIN; r.w = UINT32_MAX; r.h = UINT32_MAX;
    check(nxw_clip_rect(&r, 100, 100, &o) == 1 && o.x == 0 && o.w == 100, "rect covering everything");

    printf("%d checks\n", g_checks);
    printf(g_fail ? "RESULT: FAIL\n" : "RESULT: PASS\n");
    return g_fail;
}
