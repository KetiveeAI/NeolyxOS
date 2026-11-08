/*
 * nxlibc host test: stdio streams over NeolyxOS file syscalls, and the
 * scanf family. Files are created in the current directory and removed.
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <nx/sys.h>
#include "libc_test.h"

#define F1 "nxlibc_t1.tmp"
#define F2 "nxlibc_t2.tmp"

static void test_write_read(void) {
    FILE *f = fopen(F1, "w");
    CHECK(f != NULL);
    if (!f) return;
    CHECK(fputs("line one\n", f) >= 0);
    CHECK_INT(fprintf(f, "%d %s %.2f\n", 42, "two", 3.14159), 12);
    CHECK_INT(fputc('Z', f), 'Z');
    CHECK_INT(fwrite("\nabc", 1, 4, f), 4);
    CHECK_INT(fclose(f), 0);

    f = fopen(F1, "r");
    CHECK(f != NULL);
    if (!f) return;
    char line[64];
    CHECK_STR(fgets(line, sizeof line, f), "line one\n");
    int n; char word[16]; double d;
    CHECK_INT(fscanf(f, "%d %15s %lf", &n, word, &d), 3);
    CHECK_INT(n, 42);
    CHECK_STR(word, "two");
    CHECK(d == 3.14);
    CHECK_INT(fgetc(f), '\n');
    CHECK_INT(fgetc(f), 'Z');
    CHECK_INT(ungetc('Y', f), 'Y');
    CHECK_INT(fgetc(f), 'Y');
    CHECK_STR(fgets(line, sizeof line, f), "\n");
    CHECK_STR(fgets(line, 3, f), "ab");
    CHECK_STR(fgets(line, sizeof line, f), "c");
    CHECK(fgets(line, sizeof line, f) == NULL);
    CHECK(feof(f) && !ferror(f));
    clearerr(f);
    CHECK(!feof(f));
    CHECK_INT(fgetc(f), EOF);

    /* positioning */
    CHECK_INT(fseek(f, 5, SEEK_SET), 0);
    CHECK_INT(ftell(f), 5);
    CHECK_INT(fgetc(f), 'o');
    fpos_t pos;
    CHECK_INT(fgetpos(f, &pos), 0);
    CHECK_INT(fseek(f, -3, SEEK_END), 0);
    CHECK_INT(fgetc(f), 'a');
    CHECK_INT(fsetpos(f, &pos), 0);
    CHECK_INT(fgetc(f), 'n');
    rewind(f);
    CHECK_INT(ftell(f), 0);
    char raw[9];
    CHECK_INT(fread(raw, 1, 8, f), 8);
    raw[8] = 0;
    CHECK_STR(raw, "line one");
    errno = 0;
    CHECK_INT(fseek(f, 0, 7), -1);
    CHECK_INT(errno, EINVAL);
    CHECK_INT(fputc('x', f), EOF);       /* opened for reading only */
    CHECK(ferror(f));
    CHECK_INT(fclose(f), 0);
}

static void test_modes(void) {
    /* "a" appends */
    FILE *f = fopen(F1, "a");
    CHECK(f != NULL);
    if (!f) return;
    fputs("+end", f);
    fclose(f);
    f = fopen(F1, "r");
    char buf[128] = {0};
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    CHECK(n > 4 && strcmp(buf + n - 4, "+end") == 0);

    /* "w+" truncates, then read back after a seek */
    f = fopen(F1, "w+");
    CHECK(f != NULL);
    if (!f) return;
    fputs("hello world", f);
    CHECK_INT(fseek(f, 6, SEEK_SET), 0);
    CHECK_STR(fgets(buf, sizeof buf, f), "world");
    fclose(f);

    /* "r+" overwrites in place */
    f = fopen(F1, "r+");
    CHECK(f != NULL);
    if (!f) return;
    CHECK_INT(fgetc(f), 'h');
    CHECK_INT(fseek(f, 0, SEEK_CUR), 0);
    fputs("ELLO", f);
    fclose(f);
    f = fopen(F1, "r");
    CHECK_STR(fgets(buf, sizeof buf, f), "hELLO world");
    fclose(f);

    /* "x": exclusive create */
    errno = 0;
    CHECK(fopen(F1, "wx") == NULL);
    CHECK_INT(errno, EEXIST);
    remove(F2);
    f = fopen(F2, "wx");
    CHECK(f != NULL);
    if (f) fclose(f);

    /* bad modes and missing files */
    errno = 0;
    CHECK(fopen(F1, "q") == NULL && errno == EINVAL);
    errno = 0;
    CHECK(fopen("nxlibc_missing.tmp", "r") == NULL);
    CHECK_INT(errno, ENOENT);
    errno = 0;
    CHECK(fopen("nxlibc_no_dir/x.tmp", "w") == NULL);
    CHECK(errno == ENOENT || errno == ENOTDIR);
    static char longname[400];
    memset(longname, 'a', sizeof longname - 1);
    errno = 0;
    CHECK(fopen(longname, "r") == NULL && errno == ENAMETOOLONG);
    CHECK(fopen(NULL, "r") == NULL);
}

static void test_rename_remove(void) {
    remove(F2);
    CHECK_INT(rename(F1, F2), 0);
    errno = 0;
    CHECK(fopen(F1, "r") == NULL && errno == ENOENT);
    FILE *f = fopen(F2, "r");
    CHECK(f != NULL);
    if (f) fclose(f);
    CHECK_INT(remove(F2), 0);
    errno = 0;
    CHECK_INT(remove(F2), -1);
    CHECK_INT(errno, ENOENT);
    errno = 0;
    CHECK_INT(rename("nxlibc_missing.tmp", F2), -1);
    CHECK_INT(errno, ENOENT);
}

static void test_big_file(void) {
    FILE *f = fopen(F1, "w");
    CHECK(f != NULL);
    if (!f) return;
    for (int i = 0; i < 20000; i++) fprintf(f, "%05d\n", i);
    CHECK_INT(ftell(f), 120000);
    CHECK_INT(fclose(f), 0);

    f = fopen(F1, "r");
    char line[16];
    int ok = 1, count = 0;
    while (fgets(line, sizeof line, f)) {
        int v = -1;
        if (sscanf(line, "%d", &v) != 1 || v != count) ok = 0;
        count++;
    }
    CHECK(ok);
    CHECK_INT(count, 20000);
    /* unbuffered reads see the same bytes */
    rewind(f);
    CHECK_INT(setvbuf(f, NULL, _IONBF, 0), -1);   /* only before the first I/O */
    fclose(f);
    f = fopen(F1, "r");
    CHECK_INT(setvbuf(f, NULL, _IONBF, 0), 0);
    CHECK_INT(fseek(f, 6 * 12345, SEEK_SET), 0);
    CHECK_STR(fgets(line, sizeof line, f), "12345\n");
    fclose(f);
    remove(F1);
}

static void test_many_files(void) {
    /* the kernel allows 16 descriptors per process; fopen must fail cleanly */
    FILE *fs[40];
    int n = 0;
    while (n < 40) {
        char name[32];
        snprintf(name, sizeof name, "nxlibc_m%02d.tmp", n);
        fs[n] = fopen(name, "w");
        if (!fs[n]) break;
        n++;
    }
    CHECK(n >= 12);
    for (int i = 0; i < n; i++) {
        char name[32];
        snprintf(name, sizeof name, "nxlibc_m%02d.tmp", i);
        CHECK_INT(fclose(fs[i]), 0);
        remove(name);
    }
}

static void test_sscanf(void) {
    int a = 0, b = 0, cnt = 0;
    unsigned u = 0;
    char s[16], set[16];
    double d = 0;
    float f = 0;
    long long ll = 0;
    CHECK_INT(sscanf("12 34", "%d %d", &a, &b), 2);
    CHECK(a == 12 && b == 34);
    CHECK_INT(sscanf("  -7x", "%d%n", &a, &cnt), 1);
    CHECK(a == -7 && cnt == 4);
    CHECK_INT(sscanf("ff 0x1F 017", "%x %i %i", &u, &a, &b), 3);
    CHECK(u == 255 && a == 31 && b == 15);
    CHECK_INT(sscanf("abc,def", "%15[^,],%15s", set, s), 2);
    CHECK_STR(set, "abc");
    CHECK_STR(s, "def");
    CHECK_INT(sscanf("2.5e3 1.25", "%lf %f", &d, &f), 2);
    CHECK(d == 2500.0 && f == 1.25f);
    CHECK_INT(sscanf("9223372036854775807", "%lld", &ll), 1);
    CHECK(ll == 9223372036854775807LL);
    CHECK_INT(sscanf("", "%d", &a), EOF);
    CHECK_INT(sscanf("x", "%d", &a), 0);
    CHECK_INT(sscanf("10%", "%d%%", &a), 1);
    CHECK_INT(sscanf("key = value", "key = %15s", s), 1);
    CHECK_STR(s, "value");
    CHECK_INT(sscanf("abcdef", "%3c", set), 1);
    CHECK(memcmp(set, "abc", 3) == 0);
    CHECK_INT(sscanf("12345", "%3d%d", &a, &b), 2);
    CHECK(a == 123 && b == 45);
    CHECK_INT(sscanf("1 2", "%*d %d", &a), 1);
    CHECK(a == 2);
    CHECK_INT(sscanf("inf nan", "%lf %f", &d, &f), 2);
    CHECK(d > 1e308 && f != f);
}

static void test_std_streams(void) {
    /* stdout works in both ABIs (debug log today, fd 1 with FILEIO) */
    CHECK(printf("  stdout ok (%s)\n", nxsys_abi_has(NXSYS_ABI_FLAG_FILEIO) ? "fd 1" : "debug log") > 0);
    CHECK_INT(fflush(stdout), 0);
    CHECK(fprintf(stderr, "  stderr ok\n") > 0);
    CHECK_INT(fflush(NULL), 0);
    /* standard streams cannot be closed for good: reopen is not needed
     * to keep printing to the log */
    CHECK_INT(putchar('.'), '.');
    CHECK(puts("") >= 0);
}

int main(void) {
    test_write_read();
    test_modes();
    test_rename_remove();
    test_big_file();
    test_many_files();
    test_sscanf();
    test_std_streams();
    return t_finish("libc_stdio");
}
