/*
 * nxlibc host test: string.h, ctype.h, qsort/bsearch, strtol family,
 * rand, getenv, errno/strerror, setjmp/longjmp, atexit, inttypes.h.
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <inttypes.h>
#include <setjmp.h>
#include <assert.h>
#include <nx/sys.h>
#include <nx/libc.h>
#include "libc_test.h"

static void test_mem(void) {
    char a[64], b[64];
    for (int i = 0; i < 64; i++) a[i] = (char)i;
    CHECK(memcpy(b, a, 64) == b);
    CHECK(memcmp(a, b, 64) == 0);
    /* overlapping moves, both directions, every small offset */
    int bad = 0;
    for (int off = 1; off < 20; off++) {
        for (int i = 0; i < 64; i++) a[i] = (char)i;
        memmove(a + off, a, 40);
        for (int i = 0; i < 40; i++) if (a[i + off] != (char)i) bad++;
        for (int i = 0; i < 64; i++) a[i] = (char)i;
        memmove(a, a + off, 40);
        for (int i = 0; i < 40; i++) if (a[i] != (char)(i + off)) bad++;
    }
    CHECK_INT(bad, 0);
    for (int i = 0; i < 64; i++) a[i] = (char)i;
    memset(a, 0xAB, 33);
    CHECK((unsigned char)a[0] == 0xAB && (unsigned char)a[32] == 0xAB && a[33] == 33);
    /* memcmp compares as unsigned char */
    CHECK(memcmp("\x80", "\x01", 1) > 0);
    CHECK(memcmp("abcdefgh1", "abcdefgh2", 9) < 0);
    CHECK(memcmp("abcdefgh2", "abcdefgh1", 9) > 0);
    CHECK(memcmp("x", "y", 0) == 0);
    CHECK(memchr("hello", 'l', 5) == (const char *)"hello" + 2 || memchr("hello", 'l', 5) != NULL);
    const char *h = "hello";
    CHECK(memchr(h, 'o', 5) == h + 4);
    CHECK(memchr(h, 'o', 4) == NULL);
    CHECK(memchr(h, 0, 6) == h + 5);
    char c[16];
    CHECK(memccpy(c, "abc:def", ':', 7) == c + 4);
    CHECK(memcmp(c, "abc:", 4) == 0);
    CHECK(memccpy(c, "abc", 'z', 3) == NULL);
}

static void test_str(void) {
    char buf[32];
    CHECK_INT(strlen(""), 0);
    CHECK_INT(strlen("abcdefghijklmnopqrstuvwxyz"), 26);
    CHECK_INT(strnlen("abc", 2), 2);
    CHECK_INT(strnlen("abc", 10), 3);
    strcpy(buf, "abc");
    CHECK_STR(strcat(buf, "def"), "abcdef");
    CHECK_STR(strncat(buf, "ghijk", 2), "abcdefgh");
    memset(buf, 'X', sizeof buf);
    strncpy(buf, "ab", 5);
    CHECK(buf[0] == 'a' && buf[2] == 0 && buf[4] == 0 && buf[5] == 'X');
    strncpy(buf, "abcdef", 3);
    CHECK(buf[2] == 'c' && buf[3] == 0);   /* buf[3] still 0 from before */
    CHECK(strcmp("abc", "abd") < 0);
    CHECK(strcmp("abd", "abc") > 0);
    CHECK(strcmp("abc", "abc") == 0);
    CHECK(strcmp("ab", "abc") < 0);
    CHECK(strcmp("\xff", "a") > 0);
    CHECK(strncmp("abcx", "abcy", 3) == 0);
    CHECK(strncmp("abcx", "abcy", 4) < 0);
    CHECK(strcoll("a", "b") < 0);
    const char *s = "hello world";
    CHECK(strchr(s, 'o') == s + 4);
    CHECK(strrchr(s, 'o') == s + 7);
    CHECK(strchr(s, 0) == s + 11);
    CHECK(strrchr(s, 0) == s + 11);
    CHECK(strchr(s, 'z') == NULL);
    CHECK(strstr(s, "wor") == s + 6);
    CHECK(strstr(s, "") == s);
    CHECK(strstr(s, "worlds") == NULL);
    CHECK(strstr("aaab", "aab") != NULL);
    CHECK_INT(strspn("abcabcx", "abc"), 6);
    CHECK_INT(strcspn("abc,def", ",;"), 3);
    CHECK(strpbrk(s, "wr") == s + 6);
    CHECK(strpbrk(s, "XYZ") == NULL);
    char tok[] = "  a,b;;c  ";
    char *save = NULL;
    CHECK_STR(strtok_r(tok, " ,;", &save), "a");
    CHECK_STR(strtok_r(NULL, " ,;", &save), "b");
    CHECK_STR(strtok_r(NULL, " ,;", &save), "c");
    CHECK(strtok_r(NULL, " ,;", &save) == NULL);
    char tok2[] = "x y";
    CHECK_STR(strtok(tok2, " "), "x");
    CHECK_STR(strtok(NULL, " "), "y");
    CHECK(strtok(NULL, " ") == NULL);
    char *d = strdup("copy me");
    CHECK_STR(d, "copy me");
    free(d);
    d = strndup("copy me", 4);
    CHECK_STR(d, "copy");
    free(d);
    size_t n = strxfrm(buf, "xyz", sizeof buf);
    CHECK(n == 3 && strcmp(buf, "xyz") == 0);
}

static void test_ctype(void) {
    int bad = 0;
    for (int ch = -1; ch < 256; ch++) {
        int ascii = ch >= 0 && ch < 128;
        int digit = ch >= '0' && ch <= '9';
        int upper = ch >= 'A' && ch <= 'Z';
        int lower = ch >= 'a' && ch <= 'z';
        int alpha = upper || lower;
        int xdig = digit || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F');
        int space = ch == ' ' || (ch >= '\t' && ch <= '\r');
        int cntrl = ascii && (ch < 32 || ch == 127);
        int print = ascii && ch >= 32 && ch < 127;
        int graph = print && ch != ' ';
        int punct = graph && !alpha && !digit;
        int blank = ch == ' ' || ch == '\t';
        if (!!isdigit(ch) != digit || !!isupper(ch) != upper || !!islower(ch) != lower ||
            !!isalpha(ch) != alpha || !!isalnum(ch) != (alpha || digit) || !!isxdigit(ch) != xdig ||
            !!isspace(ch) != space || !!iscntrl(ch) != cntrl || !!isprint(ch) != print ||
            !!isgraph(ch) != graph || !!ispunct(ch) != punct || !!isblank(ch) != blank) {
            if (bad++ < 3) printf("  FAIL ctype class of %d\n", ch);
        }
        int up = lower ? ch - 32 : ch, lo = upper ? ch + 32 : ch;
        if (toupper(ch) != up || tolower(ch) != lo) {
            if (bad++ < 3) printf("  FAIL case mapping of %d\n", ch);
        }
    }
    CHECK_INT(bad, 0);
}

static int cmp_int(const void *a, const void *b) {
    int x = *(const int *)a, y = *(const int *)b;
    return (x > y) - (x < y);
}

static uint64_t g_seed = 0xB7E151628AED2A6Bull;
static uint64_t rnd(void) {
    g_seed ^= g_seed >> 12;
    g_seed ^= g_seed << 25;
    g_seed ^= g_seed >> 27;
    return g_seed * 0x2545F4914F6CDD1Dull;
}

static int sorted(const int *v, size_t n) {
    for (size_t i = 1; i < n; i++) if (v[i - 1] > v[i]) return 0;
    return 1;
}

static void test_qsort(void) {
    enum { N = 100000 };
    static int v[N];
    long long sum0 = 0, sum1 = 0;
    for (int i = 0; i < N; i++) { v[i] = (int)(rnd() % 1000); sum0 += v[i]; }
    qsort(v, N, sizeof v[0], cmp_int);
    for (int i = 0; i < N; i++) sum1 += v[i];
    CHECK(sorted(v, N) && sum0 == sum1);
    qsort(v, N, sizeof v[0], cmp_int);           /* already sorted */
    CHECK(sorted(v, N));
    for (int i = 0; i < N; i++) v[i] = N - i;    /* descending */
    qsort(v, N, sizeof v[0], cmp_int);
    CHECK(sorted(v, N) && v[0] == 1);
    for (int i = 0; i < N; i++) v[i] = 7;        /* all equal */
    qsort(v, N, sizeof v[0], cmp_int);
    CHECK(sorted(v, N));
    /* organ pipe: the classic worst case for simple quicksorts */
    for (int i = 0; i < N; i++) v[i] = (i < N / 2) ? i : N - i;
    qsort(v, N, sizeof v[0], cmp_int);
    CHECK(sorted(v, N));
    /* odd element sizes */
    char words[6][8] = { "pear", "apple", "fig", "kiwi", "date", "lime" };
    qsort(words, 6, 8, (int (*)(const void *, const void *))strcmp);
    CHECK_STR(words[0], "apple");
    CHECK_STR(words[5], "pear");
    qsort(NULL, 0, 4, cmp_int);

    for (int i = 0; i < 1000; i++) v[i] = i * 2;
    int key = 446;
    int *hit = bsearch(&key, v, 1000, sizeof v[0], cmp_int);
    CHECK(hit && *hit == 446);
    key = 447;
    CHECK(bsearch(&key, v, 1000, sizeof v[0], cmp_int) == NULL);
    key = -1;
    CHECK(bsearch(&key, v, 1000, sizeof v[0], cmp_int) == NULL);
}

static void test_strtol(void) {
    char *end;
    CHECK_INT(strtol("  -123xyz", &end, 10), -123);
    CHECK_STR(end, "xyz");
    CHECK_INT(strtol("0x1F", NULL, 0), 31);
    CHECK_INT(strtol("0X1f", NULL, 16), 31);
    CHECK_INT(strtol("017", NULL, 0), 15);
    CHECK_INT(strtol("z", NULL, 36), 35);
    CHECK_INT(strtol("101", NULL, 2), 5);
    const char *s = "0x";
    CHECK_INT(strtol(s, &end, 16), 0);
    CHECK(end == s + 1);                        /* only the "0" is used */
    s = "   ";
    CHECK_INT(strtol(s, &end, 10), 0);
    CHECK(end == s);
    errno = 0;
    CHECK(strtol("9223372036854775808", NULL, 10) == LONG_MAX && errno == ERANGE);
    errno = 0;
    CHECK(strtol("-9223372036854775809", NULL, 10) == LONG_MIN && errno == ERANGE);
    errno = 0;
    CHECK(strtol("-9223372036854775808", NULL, 10) == LONG_MIN && errno == 0);
    errno = 0;
    CHECK(strtoul("18446744073709551615", NULL, 10) == ULONG_MAX && errno == 0);
    errno = 0;
    CHECK(strtoul("18446744073709551616", NULL, 10) == ULONG_MAX && errno == ERANGE);
    CHECK(strtoul("-1", NULL, 10) == ULONG_MAX);  /* ISO: negated in the return type */
    errno = 0;
    CHECK(strtol("12", NULL, 1) == 0 && errno == EINVAL);
    errno = 0;
    CHECK(strtol("12", NULL, 37) == 0 && errno == EINVAL);
    CHECK_INT(strtoll("-42", NULL, 10), -42);
    CHECK(strtoull("ffffffffffffffff", NULL, 16) == UINT64_MAX);
    CHECK_INT(atoi("  77abc"), 77);
    CHECK_INT(atoi("99999999999"), INT_MAX);     /* nxlibc clamps instead of UB */
    CHECK_INT(atol("-5"), -5);
    CHECK_INT(atoll("123456789012"), 123456789012LL);
    CHECK(strtoimax("-9", NULL, 10) == -9);
    CHECK(strtoumax("10", NULL, 16) == 16);
}

static void test_stdlib_misc(void) {
    CHECK_INT(abs(-5), 5);
    CHECK_INT(labs(-5L), 5);
    CHECK(llabs(-5LL) == 5);
    div_t q = div(-7, 2);
    CHECK(q.quot == -3 && q.rem == -1);
    ldiv_t lq = ldiv(7L, -2L);
    CHECK(lq.quot == -3 && lq.rem == 1);
    lldiv_t llq = lldiv(100LL, 7LL);
    CHECK(llq.quot == 14 && llq.rem == 2);
    imaxdiv_t iq = imaxdiv(9, 4);
    CHECK(iq.quot == 2 && iq.rem == 1);
    CHECK(imaxabs(-3) == 3);

    srand(1);
    int first = rand();
    int in_range = 1;
    for (int i = 0; i < 10000; i++) {
        int r = rand();
        if (r < 0 || r > RAND_MAX) in_range = 0;
    }
    CHECK(in_range);
    srand(1);
    CHECK_INT(rand(), first);

    CHECK(getenv("NXLIBC_SURELY_UNSET") == NULL);
    CHECK(getenv(NULL) == NULL);
    CHECK(getenv("") == NULL);
    CHECK_INT(system(NULL), 0);
    errno = 0;
    CHECK(system("ls") == -1 && errno == ENOSYS);
}

static void test_errno_strings(void) {
    CHECK_STR(strerror(0), "No error");
    CHECK_STR(strerror(ENOENT), "No such file or directory");
    CHECK_STR(strerror(EDOM), "Argument out of domain");
    CHECK_STR(strerror(ECONNREFUSED), "Connection refused");
    CHECK_STR(strerror(9999), "Unknown error 9999");
    CHECK_STR(strerror(-3), "Unknown error -3");
    errno = 0;
    CHECK_INT(errno, 0);
    errno = EIO;
    CHECK_INT(errno, EIO);
    errno = 0;
}

static jmp_buf g_jb;
static int g_depth;

static void jump_down(int n) {
    g_depth = n;
    if (n == 0) longjmp(g_jb, 0);    /* 0 is delivered as 1 */
    jump_down(n - 1);
}

static void test_setjmp(void) {
    volatile int local = 1;
    volatile double fp = 1.5;
    int r = setjmp(g_jb);
    if (r == 0) {
        local = 2;
        fp = 2.5;
        jump_down(100);
        CHECK(0);                    /* not reached */
    }
    CHECK_INT(r, 1);
    CHECK_INT(local, 2);
    CHECK(fp == 2.5);
    CHECK_INT(g_depth, 0);
    r = setjmp(g_jb);
    if (r == 0) longjmp(g_jb, 42);
    CHECK_INT(r, 42);
}

static void test_misc_headers(void) {
    char buf[64];
    snprintf(buf, sizeof buf, "%" PRId64 " %" PRIu32 " %" PRIx64, (int64_t)-5, (uint32_t)7, (uint64_t)255);
    CHECK_STR(buf, "-5 7 ff");
    CHECK(INT_MAX == 2147483647 && LONG_MAX == 9223372036854775807L && CHAR_BIT == 8);
    assert(1 + 1 == 2);
    CHECK(nxc_version() != NULL);
    CHECK(nxsys_abi_flags() != 0);
}

static int g_handler_order;
static void handler_last(void) {
    /* registered first, so it runs last */
    if (g_handler_order != 1) {
        printf("  FAIL atexit order\n");
        _Exit(3);
    }
}
static void handler_first(void) { g_handler_order = 1; }

int main(void) {
    test_mem();
    test_str();
    test_ctype();
    test_qsort();
    test_strtol();
    test_stdlib_misc();
    test_errno_strings();
    test_setjmp();
    test_misc_headers();
    CHECK_INT(atexit(handler_last), 0);
    CHECK_INT(atexit(handler_first), 0);
    return t_finish("libc_misc");
}
