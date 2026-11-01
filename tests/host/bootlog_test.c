/*
 * Host unit test for the kernel boot log (kernel/src/video/bootlog.c): the
 * log ring, the live on-screen mirror and the crash screen.
 *
 * The test draws into a framebuffer below 4 GB (MAP_32BIT), reads the text
 * back by matching each character cell against the kernel font, and checks
 * what a person would see.
 *
 * Build and run on the development machine (Linux x86-64):
 *   gcc -std=gnu11 -Wall -Wextra -iquote ../../kernel/include -iquote ../../kernel/src/video \
 *       -o /tmp/bootlog_test bootlog_test.c ../../kernel/src/video/bootlog.c
 *   /tmp/bootlog_test
 *
 * Copyright (c) 2025 KetiveeAI
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include "video/bootlog.h"
#include "font.h"

#define W 640
#define H 400
#define PITCH (W * 4)
#define MARGIN 8
#define COLS ((W - 2 * MARGIN) / FONT_WIDTH)
#define ROWS ((H - 2 * MARGIN) / FONT_HEIGHT)

static uint32_t *g_fb;
static int g_fail;

static void check(int ok, const char *what) {
    printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) g_fail = 1;
}

/* Read back the character drawn in one text cell ('\0' if none matches). */
static char cell_char(uint32_t col, uint32_t row) {
    uint32_t x0 = MARGIN + col * FONT_WIDTH, y0 = MARGIN + row * FONT_HEIGHT;
    for (int c = FONT_FIRST_CHAR; c <= FONT_LAST_CHAR; c++) {
        const uint8_t *g = font_get_glyph((char)c);
        int match = 1;
        for (int y = 0; y < FONT_HEIGHT && match; y++) {
            for (int x = 0; x < FONT_WIDTH; x++) {
                uint32_t px = g_fb[(y0 + y) * (PITCH / 4) + x0 + x];
                int on = (g[y] & (0x80 >> x)) != 0;
                int lit = (px == 0x00E0E0E0u || px == 0x00FFFFFFu);
                if (on != lit) { match = 0; break; }
            }
        }
        if (match) return (char)c;
    }
    return '\0';
}

/* Text of one row, trailing spaces removed. */
static void row_text(uint32_t row, char *out, size_t n) {
    size_t i = 0;
    for (uint32_t col = 0; col < COLS && i + 1 < n; col++) {
        char c = cell_char(col, row);
        out[i++] = c ? c : '#';
    }
    while (i > 0 && out[i - 1] == ' ') i--;
    out[i] = '\0';
}

static void log_str(const char *s) {
    while (*s) bootlog_putc(*s++);
}

int main(void) {
    void *mem = mmap(NULL, PITCH * H, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    if (mem == MAP_FAILED) {
        printf("RESULT: FAIL (no low memory for the test framebuffer)\n");
        return 1;
    }
    g_fb = mem;
    char text[256], want[64];

    /* Argument checks */
    check(bootlog_attach_fb(0, W, H, PITCH) == -1, "attach rejects a NULL framebuffer");
    check(bootlog_attach_fb((uintptr_t)g_fb, W, H, W * 2) == -1, "attach rejects pitch < width*4");
    check(bootlog_attach_fb(0x1FFFFF000ULL, W, H, PITCH) == -2, "attach rejects a framebuffer above 4 GB");
    check(bootlog_attach_fb(0xFFFFFFFFFFFFF000ULL, W, H, PITCH) == -2, "attach rejects an address that wraps");

    /* Logged before any screen: kept in the ring, nothing drawn */
    log_str("[BOOT] early line\r\n");
    check(g_fb[0] == 0, "nothing drawn before a framebuffer is attached");

    check(bootlog_attach_fb((uintptr_t)g_fb, W, H, PITCH) == 0, "attach accepts a valid framebuffer");
    check(!bootlog_mirror_active(), "mirror is off until requested");

    /* Starting the mirror replays the early line */
    bootlog_set_mirror(1);
    row_text(0, text, sizeof text);
    check(strcmp(text, "[BOOT] early line") == 0, "mirror replays lines logged before it started");

    /* Live lines, with UTF-8 box characters drawn as one '?' each */
    log_str("\xE2\x95\x94\xE2\x95\x90 live\n");
    row_text(1, text, sizeof text);
    check(strcmp(text, "?? live") == 0, "live line drawn; UTF-8 becomes '?'");

    /* Wrap to the top instead of scrolling, with a blank row after the newest */
    for (int i = 0; i < ROWS; i++) {
        snprintf(want, sizeof want, "wrap %d\n", i);
        log_str(want);
    }
    /* rows 0..1 used, then ROWS more lines: newest is "wrap ROWS-1" */
    uint32_t newest = (2 + ROWS - 1) % ROWS;
    row_text(newest, text, sizeof text);
    snprintf(want, sizeof want, "wrap %d", ROWS - 1);
    check(strcmp(text, want) == 0, "mirror wraps to the top");
    row_text((newest + 2) % ROWS, text, sizeof text);
    check(text[0] == '\0', "a blank row marks the wrap point");

    bootlog_set_mirror(0);
    uint32_t before = g_fb[MARGIN * (PITCH / 4) + MARGIN + 1];
    log_str("not drawn\n");
    check(g_fb[MARGIN * (PITCH / 4) + MARGIN + 1] == before && !bootlog_mirror_active(),
          "nothing drawn after the mirror stops");

    /* Crash screen: title, then the newest lines, newest at the bottom */
    for (int i = 0; i < 500; i++) {
        snprintf(want, sizeof want, "line %d\n", i);
        log_str(want);
    }
    /* One line longer than the screen: wraps over two rows */
    char longline[COLS + 12];
    memset(longline, 'x', sizeof longline - 2);
    longline[sizeof longline - 2] = '\n';
    longline[sizeof longline - 1] = '\0';
    log_str(longline);
    log_str("last words\n");

    bootlog_show_crash("KERNEL PANIC");
    row_text(0, text, sizeof text);
    check(strncmp(text, "KERNEL PANIC", 12) == 0, "crash screen shows the title");
    row_text(ROWS - 1, text, sizeof text);
    check(strcmp(text, "last words") == 0, "newest line is on the bottom row");
    row_text(ROWS - 2, text, sizeof text);
    check(strspn(text, "x") == strlen(text) && strlen(text) == 10, "long line wraps (second part)");
    row_text(ROWS - 3, text, sizeof text);
    check(strspn(text, "x") == COLS, "long line wraps (first part)");
    row_text(ROWS - 4, text, sizeof text);
    check(strcmp(text, "line 499") == 0, "lines before the long one are in order");
    row_text(2, text, sizeof text);
    snprintf(want, sizeof want, "line %d", 499 - (ROWS - 2 - 3) + 1);
    check(strcmp(text, want) == 0, "screen is filled from row 2 with consecutive lines");

    /* Ring overflow: far more than BOOTLOG_RING_SIZE bytes, newest still shown */
    for (int i = 0; i < 5000; i++) {
        snprintf(want, sizeof want, "spam %05d\n", i);
        log_str(want);
    }
    bootlog_show_crash(NULL);
    row_text(ROWS - 1, text, sizeof text);
    check(strcmp(text, "spam 04999") == 0, "after ring overflow the newest line is shown");

    /* Summary: only matching lines, oldest first, from row 2 */
    log_str("[PS2] No PS/2 controller\n");
    for (int i = 0; i < 300; i++) {
        snprintf(want, sizeof want, "noise %d\n", i);
        log_str(want);
    }
    log_str("[USB] controller left alone\n");
    log_str("[BOOT] WARNING: something\n");
    static const char *const pats[] = { "[PS2]", "[USB]", "WARNING" };
    bootlog_show_summary("SUMMARY", pats, 3);
    row_text(0, text, sizeof text);
    check(strncmp(text, "SUMMARY", 7) == 0, "summary shows the title");
    row_text(2, text, sizeof text);
    check(strcmp(text, "[PS2] No PS/2 controller") == 0, "summary row 2 is the oldest match");
    row_text(3, text, sizeof text);
    check(strcmp(text, "[USB] controller left alone") == 0, "summary keeps order");
    row_text(4, text, sizeof text);
    check(strcmp(text, "[BOOT] WARNING: something") == 0, "summary includes WARNING lines");
    row_text(5, text, sizeof text);
    check(text[0] == '\0', "summary has no other lines");
    /* More matches than rows: the newest are kept */
    for (int i = 0; i < 100; i++) {
        snprintf(want, sizeof want, "[USB] n%d\n", i);
        log_str(want);
    }
    bootlog_show_summary(NULL, pats, 3);
    row_text(ROWS - 1, text, sizeof text);
    check(strcmp(text, "[USB] n99") == 0, "summary keeps the newest matches when full");

    /* Boot flags word */
    uint32_t flags = 99, fmt = 99;
    check(bootlog_boot_flags(0, &flags, &fmt) == 0 && flags == 0 && fmt == NEOLYX_PIXFMT_BGRX8,
          "boot flags: old loader (0) means no flags, BGR");
    uint64_t word = ((uint64_t)NEOLYX_BOOT_FLAGS_MAGIC << 32) |
                    ((uint64_t)NEOLYX_PIXFMT_RGBX8 << NEOLYX_BOOT_FLAGS_PIXFMT_SHIFT) |
                    NEOLYX_BOOT_FLAG_VERBOSE | NEOLYX_BOOT_FLAG_WRITE_PROTECT;
    check(bootlog_boot_flags(word, &flags, &fmt) == 1 &&
          flags == (NEOLYX_BOOT_FLAG_VERBOSE | NEOLYX_BOOT_FLAG_WRITE_PROTECT) &&
          fmt == NEOLYX_PIXFMT_RGBX8, "boot flags: decoded");
    check(bootlog_boot_flags(0x12345678ULL << 32 | 3, &flags, NULL) == 0 && flags == 0,
          "boot flags: wrong magic ignored");

    printf(g_fail ? "RESULT: FAIL\n" : "RESULT: PASS\n");
    return g_fail;
}
