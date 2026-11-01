/*
 * Host unit test for the byte ring (kernel/src/core/nx_ring.c) and the PTY
 * line discipline (kernel/src/core/nx_ldisc.c), section 8 of
 * docs/kernel/proposals/beta-process-abi.md.
 *
 * Checks:
 *   - ring: wrap-around, partial writes when full, reads in pieces, drop,
 *     NULL and zero-capacity rings;
 *   - canonical input: a line is readable only after Enter, CR becomes LF,
 *     echo with CR LF, Backspace/Delete erase one character (a whole UTF-8
 *     sequence, two columns for a "^X" echo), Ctrl-U erases the line,
 *     Ctrl-D on an empty line is end of file, Ctrl-D after text hands over
 *     the text without a newline, a short read gets a line in pieces, the
 *     line limit drops extra characters but keeps room for Enter;
 *   - Ctrl-C with ISIG: reports the event, discards typed and queued input,
 *     echoes ^C; without ISIG it is an ordinary character;
 *   - back-pressure: input stops when the slave queue or the record queue is
 *     full and continues after the slave reads; echo is dropped, never
 *     blocking, when the master output is full;
 *   - raw mode: bytes pass straight through; switching modes keeps queued
 *     bytes;
 *   - output: ONLCR, a "\n" taken only when both bytes fit;
 *   - bad arguments are refused.
 *
 * Build and run from the repository root:
 *   gcc -std=c11 -O1 -Wall -Wextra -Ikernel/include \
 *       -o /tmp/nx_ldisc_test tests/host/nx_ldisc_test.c \
 *       kernel/src/core/nx_ring.c kernel/src/core/nx_ldisc.c
 *   /tmp/nx_ldisc_test
 *
 * Copyright (c) 2025 KetiveeAI
 */

#include <stdio.h>
#include <string.h>

#include "core/nx_ring.h"
#include "core/nx_ldisc.h"

static int failures;
static int checks;

#define CHECK(cond, msg) do {                                           \
        checks++;                                                       \
        if (!(cond)) {                                                  \
            failures++;                                                 \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, msg);        \
        }                                                               \
    } while (0)

static uint8_t in_buf[4096];
static uint8_t out_buf[4096];

static void setup(nx_ldisc_t *ld, uint32_t in_cap, uint32_t out_cap, uint32_t mode) {
    int rc = nx_ldisc_init(ld, in_buf, in_cap, out_buf, out_cap, mode);
    CHECK(rc == 0, "init");
}

static uint32_t type(nx_ldisc_t *ld, const char *s, uint32_t *ev) {
    return nx_ldisc_input(ld, (const uint8_t *)s, (uint32_t)strlen(s), ev);
}

/* Master output as a NUL-terminated string (drains it) */
static const char *screen(nx_ldisc_t *ld) {
    static char buf[8192];
    uint32_t n = nx_ldisc_read_output(ld, (uint8_t *)buf, sizeof(buf) - 1);
    buf[n] = '\0';
    return buf;
}

/* Slave read as a string; returns the read result */
static int32_t slave_read(nx_ldisc_t *ld, char *dst, uint32_t cap) {
    int32_t r = nx_ldisc_read(ld, (uint8_t *)dst, cap - 1);
    dst[r > 0 ? r : 0] = '\0';
    return r;
}

static void test_ring(void) {
    nx_ring_t r;
    uint8_t store[8];
    uint8_t got[16];

    nx_ring_init(&r, store, sizeof(store));
    CHECK(nx_ring_used(&r) == 0 && nx_ring_free(&r) == 8, "ring starts empty");
    CHECK(nx_ring_write(&r, (const uint8_t *)"abcdef", 6) == 6, "ring write 6");
    CHECK(nx_ring_read(&r, got, 4) == 4 && memcmp(got, "abcd", 4) == 0, "ring read 4");
    CHECK(nx_ring_write(&r, (const uint8_t *)"ghijklmn", 8) == 6, "ring partial write when full");
    CHECK(nx_ring_free(&r) == 0, "ring full");
    CHECK(nx_ring_put(&r, 'z') == 0, "put on full ring refused");
    CHECK(nx_ring_read(&r, got, 16) == 8 && memcmp(got, "efghijkl", 8) == 0,
          "ring wraps and keeps order");
    CHECK(nx_ring_read(&r, got, 1) == 0, "read from empty ring");
    nx_ring_write(&r, (const uint8_t *)"xyz", 3);
    CHECK(nx_ring_read(&r, NULL, 2) == 2 && nx_ring_used(&r) == 1, "read into NULL drops");
    nx_ring_clear(&r);
    CHECK(nx_ring_used(&r) == 0, "clear");

    nx_ring_t z;
    nx_ring_init(&z, NULL, 100);
    CHECK(nx_ring_write(&z, (const uint8_t *)"a", 1) == 0 && nx_ring_free(&z) == 0,
          "ring without storage holds nothing");
    CHECK(nx_ring_used(NULL) == 0 && nx_ring_write(NULL, store, 1) == 0, "NULL ring");
}

static void test_canonical(void) {
    nx_ldisc_t ld;
    char line[256];
    uint32_t ev;

    setup(&ld, 4096, 4096, NX_LDISC_MODE_DEFAULT);
    CHECK(type(&ld, "ls -l", &ev) == 5 && ev == 0, "typing consumed");
    CHECK(!nx_ldisc_readable(&ld), "no line before Enter");
    CHECK(nx_ldisc_read(&ld, (uint8_t *)line, 10) == NX_LDISC_EMPTY, "read before Enter is empty");
    CHECK(strcmp(screen(&ld), "ls -l") == 0, "echo of typed text");
    type(&ld, "\r", &ev);
    CHECK(strcmp(screen(&ld), "\r\n") == 0, "Enter echoes CR LF");
    CHECK(nx_ldisc_readable(&ld), "line ready");
    CHECK(slave_read(&ld, line, sizeof(line)) == 6 && strcmp(line, "ls -l\n") == 0,
          "CR becomes LF and the line is read whole");

    /* Two lines queued: each read returns one */
    type(&ld, "a\nbb\n", &ev);
    screen(&ld);
    CHECK(slave_read(&ld, line, sizeof(line)) == 2 && strcmp(line, "a\n") == 0, "first of two lines");
    CHECK(slave_read(&ld, line, sizeof(line)) == 3 && strcmp(line, "bb\n") == 0, "second of two lines");

    /* Short reads get a line in pieces */
    type(&ld, "hello\n", &ev);
    screen(&ld);
    CHECK(slave_read(&ld, line, 4) == 3 && strcmp(line, "hel") == 0, "piece 1");
    CHECK(slave_read(&ld, line, 4) == 3 && strcmp(line, "lo\n") == 0, "piece 2");
    CHECK(!nx_ldisc_readable(&ld), "line fully read");

    /* Backspace and Delete */
    type(&ld, "cat\x7f\x7fp\b", &ev);
    CHECK(strcmp(screen(&ld), "cat\b \b\b \bp\b \b") == 0, "erase echo");
    type(&ld, "d\n", &ev);
    screen(&ld);
    CHECK(slave_read(&ld, line, sizeof(line)) == 3 && strcmp(line, "cd\n") == 0, "erased text");
    type(&ld, "\x7f", &ev);
    CHECK(strcmp(screen(&ld), "") == 0, "erase on an empty line echoes nothing");

    /* UTF-8: one Backspace removes the whole sequence (e-acute is C3 A9) */
    type(&ld, "caf\xc3\xa9\x7f" "e\n", &ev);
    screen(&ld);
    CHECK(slave_read(&ld, line, sizeof(line)) == 5 && strcmp(line, "cafe\n") == 0,
          "UTF-8 sequence erased as one character");

    /* Control characters echo as ^X and take two columns to erase */
    type(&ld, "\x1b", &ev);
    CHECK(strcmp(screen(&ld), "^[") == 0, "ESC echoes as ^[");
    type(&ld, "\x7f", &ev);
    CHECK(strcmp(screen(&ld), "\b \b\b \b") == 0, "erasing ^[ clears two columns");

    /* Ctrl-U */
    type(&ld, "rm -rf\x15ls\n", &ev);
    screen(&ld);
    CHECK(slave_read(&ld, line, sizeof(line)) == 3 && strcmp(line, "ls\n") == 0, "Ctrl-U kills the line");

    /* Ctrl-D */
    type(&ld, "\x04", &ev);
    CHECK(nx_ldisc_readable(&ld), "EOF mark is readable");
    CHECK(slave_read(&ld, line, sizeof(line)) == 0, "Ctrl-D on empty line is end of file");
    CHECK(!nx_ldisc_readable(&ld), "EOF mark consumed once");
    type(&ld, "abc\x04", &ev);
    CHECK(slave_read(&ld, line, sizeof(line)) == 3 && strcmp(line, "abc") == 0,
          "Ctrl-D after text hands it over without a newline");
    CHECK(strcmp(screen(&ld), "abc") == 0, "Ctrl-D is not echoed");

    /* Line limit: extra characters are dropped, Enter still fits */
    setup(&ld, 4096, 4096, NX_LDISC_MODE_DEFAULT & ~NX_LDISC_ECHO);
    for (int i = 0; i < NX_LDISC_LINE_MAX + 10; i++) nx_ldisc_input(&ld, (const uint8_t *)"x", 1, &ev);
    CHECK(ld.input_dropped == 11, "characters past the limit are dropped");
    CHECK(type(&ld, "\n", &ev) == 1, "Enter accepted at the limit");
    static char big[2048];
    CHECK(nx_ldisc_read(&ld, (uint8_t *)big, sizeof(big)) == NX_LDISC_LINE_MAX, "full line length");
    CHECK(big[NX_LDISC_LINE_MAX - 1] == '\n', "full line ends with newline");
    CHECK(strcmp(screen(&ld), "") == 0, "no echo with ECHO off");
}

static void test_intr(void) {
    nx_ldisc_t ld;
    char line[64];
    uint32_t ev;

    setup(&ld, 4096, 4096, NX_LDISC_MODE_DEFAULT);
    type(&ld, "queued\n", &ev);
    type(&ld, "half", &ev);
    screen(&ld);
    CHECK(type(&ld, "\x03", &ev) == 1 && (ev & NX_LDISC_EV_INTR), "Ctrl-C reports INTR");
    CHECK(strcmp(screen(&ld), "^C\r\n") == 0, "Ctrl-C echo");
    CHECK(!nx_ldisc_readable(&ld), "Ctrl-C discards queued and typed input");
    type(&ld, "after\n", &ev);
    CHECK(slave_read(&ld, line, sizeof(line)) == 6 && strcmp(line, "after\n") == 0,
          "input works after Ctrl-C");

    /* Raw mode with ISIG still reports Ctrl-C */
    nx_ldisc_set_mode(&ld, NX_LDISC_ISIG);
    CHECK(type(&ld, "q\x03", &ev) == 2 && (ev & NX_LDISC_EV_INTR), "raw + ISIG reports INTR");
    CHECK(!nx_ldisc_readable(&ld), "raw input discarded by Ctrl-C");

    /* Without ISIG Ctrl-C is a character */
    nx_ldisc_set_mode(&ld, 0);
    CHECK(type(&ld, "\x03", &ev) == 1 && ev == 0, "no INTR without ISIG");
    CHECK(slave_read(&ld, line, sizeof(line)) == 1 && line[0] == 0x03, "0x03 delivered as a byte");
}

static void test_backpressure(void) {
    nx_ldisc_t ld;
    char line[64];
    uint32_t ev;

    /* Slave queue of 8 bytes: the second line does not fit until a read */
    setup(&ld, 8, 4096, NX_LDISC_MODE_DEFAULT);
    CHECK(type(&ld, "abcde\n", &ev) == 6, "first line fits");
    CHECK(type(&ld, "fgh\n", &ev) == 3, "Enter of the second line waits for room");
    CHECK(ld.line_len == 3, "typed text kept while waiting");
    CHECK(slave_read(&ld, line, sizeof(line)) == 6, "slave reads the first line");
    CHECK(type(&ld, "\n", &ev) == 1, "Enter accepted after the read");
    CHECK(slave_read(&ld, line, sizeof(line)) == 4 && strcmp(line, "fgh\n") == 0, "second line");

    /* Record queue limit */
    setup(&ld, 4096, 4096, NX_LDISC_MODE_DEFAULT & ~NX_LDISC_ECHO);
    uint32_t taken = 0;
    for (int i = 0; i < NX_LDISC_RECS + 5; i++) taken += type(&ld, "\n", &ev);
    CHECK(taken == NX_LDISC_RECS, "at most NX_LDISC_RECS queued lines");
    CHECK(type(&ld, "\x04", &ev) == 0, "EOF mark waits when the record queue is full");

    /* Raw mode: input stops when full */
    setup(&ld, 4, 4096, 0);
    CHECK(type(&ld, "123456", &ev) == 4, "raw input stops at capacity");

    /* Echo dropped, never blocking, when master output is full */
    setup(&ld, 4096, 4, NX_LDISC_MODE_DEFAULT);
    CHECK(type(&ld, "abcdef\n", &ev) == 7, "input continues while echo is lost");
    CHECK(ld.echo_dropped > 0, "lost echo counted");
    CHECK(slave_read(&ld, line, sizeof(line)) == 7, "line delivered despite lost echo");
}

static void test_raw_and_modes(void) {
    nx_ldisc_t ld;
    char line[64];
    uint32_t ev;

    setup(&ld, 4096, 4096, NX_LDISC_ECHO);
    type(&ld, "a\x7f\r", &ev);
    CHECK(slave_read(&ld, line, sizeof(line)) == 3 && memcmp(line, "a\x7f\r", 3) == 0,
          "raw mode passes bytes unchanged");
    CHECK(strcmp(screen(&ld), "a\x7f\r") == 0, "raw echo unchanged");

    /* canonical -> raw: queued line and partial line become bytes */
    setup(&ld, 4096, 4096, NX_LDISC_MODE_DEFAULT);
    type(&ld, "one\ntw", &ev);
    type(&ld, "\x04", &ev);         /* "tw" record */
    type(&ld, "\x04", &ev);         /* EOF mark */
    type(&ld, "xy", &ev);
    CHECK(nx_ldisc_set_mode(&ld, NX_LDISC_ECHO) == 0, "leave canonical");
    CHECK(slave_read(&ld, line, sizeof(line)) == 8 && strcmp(line, "one\ntwxy") == 0,
          "queued and partial text become raw bytes; EOF marks dropped");

    /* raw -> canonical: queued bytes become one record */
    type(&ld, "zz", &ev);
    CHECK(nx_ldisc_set_mode(&ld, NX_LDISC_MODE_DEFAULT) == 0, "enter canonical");
    CHECK(nx_ldisc_readable(&ld), "queued raw bytes readable");
    CHECK(slave_read(&ld, line, sizeof(line)) == 2 && strcmp(line, "zz") == 0, "raw bytes as one record");

    CHECK(nx_ldisc_set_mode(&ld, 0x100) == -1 && ld.mode == NX_LDISC_MODE_DEFAULT,
          "unknown mode bits refused");
}

static void test_output(void) {
    nx_ldisc_t ld;
    uint32_t ev;

    setup(&ld, 4096, 5, NX_LDISC_MODE_DEFAULT);
    CHECK(nx_ldisc_output(&ld, (const uint8_t *)"ab\ncd", 5) == 4, "output stops when full");
    CHECK(strcmp(screen(&ld), "ab\r\nc") == 0, "ONLCR on output");
    CHECK(nx_ldisc_output(&ld, (const uint8_t *)"1234\n", 5) == 4, "newline needs two bytes of room");
    CHECK(nx_ldisc_output_room(&ld) == 1, "one byte left");
    screen(&ld);

    nx_ldisc_set_mode(&ld, NX_LDISC_MODE_DEFAULT & ~NX_LDISC_ONLCR);
    CHECK(nx_ldisc_output(&ld, (const uint8_t *)"x\n", 2) == 2, "no ONLCR");
    CHECK(strcmp(screen(&ld), "x\n") == 0, "plain newline");
    (void)ev;
}

static void test_bad_args(void) {
    nx_ldisc_t ld;
    uint8_t b[4];
    uint32_t ev = 99;

    CHECK(nx_ldisc_init(NULL, in_buf, 8, out_buf, 8, 0) == -1, "init NULL ld");
    CHECK(nx_ldisc_init(&ld, NULL, 8, out_buf, 8, 0) == -1, "init NULL in");
    CHECK(nx_ldisc_init(&ld, in_buf, 0, out_buf, 8, 0) == -1, "init zero capacity");
    CHECK(nx_ldisc_init(&ld, in_buf, 70000, out_buf, 8, 0) == -1, "init capacity above 65535");
    setup(&ld, 8, 8, NX_LDISC_MODE_DEFAULT);
    CHECK(nx_ldisc_input(&ld, NULL, 4, &ev) == 0 && ev == 0, "input NULL source");
    CHECK(nx_ldisc_read(&ld, NULL, 4) == NX_LDISC_EMPTY, "read NULL buffer");
    CHECK(nx_ldisc_read(&ld, b, 0) == NX_LDISC_EMPTY, "read zero length");
    CHECK(nx_ldisc_output(NULL, b, 1) == 0, "output NULL ld");
    CHECK(nx_ldisc_readable(NULL) == 0, "readable NULL ld");
}

int main(void) {
    test_ring();
    test_canonical();
    test_intr();
    test_backpressure();
    test_raw_and_modes();
    test_output();
    test_bad_args();

    printf("nx_ldisc_test: %d checks, %d failures\n", checks, failures);
    printf("%s\n", failures ? "RESULT: FAIL" : "RESULT: PASS");
    return failures ? 1 : 0;
}
