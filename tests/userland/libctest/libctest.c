/*
 * libctest - checks nxlibc on NeolyxOS itself: the real kernel, real
 * syscalls, real VFS. The full test suites run on the development machine
 * (tests/host/libc_*); this program checks what only the target can:
 * start-up (argc/argv, FPU state), brk-backed malloc, stdio over the VFS,
 * the RTC and timer, and the ABI feature flags.
 *
 * Output goes to the serial log, one "[LIBCTEST]" line per section, and
 * ends with "[LIBCTEST] RESULT: PASS" or "[LIBCTEST] RESULT: FAIL".
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <math.h>
#include <setjmp.h>
#include <stdint.h>
#include <nx/sys.h>
#include <nx/libc.h>

static int g_fail;
static int g_checks;

#define CHECK(cond) do { \
    g_checks++; \
    if (!(cond)) { g_fail++; printf("[LIBCTEST] FAIL line %d: %s\n", __LINE__, #cond); } \
} while (0)

static void section(const char *name, int fails_before) {
    printf("[LIBCTEST] %-8s %s\n", name, g_fail == fails_before ? "ok" : "FAILED");
}

/* ---- start-up ---- */
static void test_startup(int argc, char **argv) {
    int f0 = g_fail;
    CHECK(argc >= 0);
    CHECK(argv != NULL);
    CHECK(argc == 0 || argv[argc] == NULL);
    printf("[LIBCTEST] argc=%d argv[0]=%s pid=%ld\n", argc, (argc > 0 && argv[0]) ? argv[0] : "(none)",
           nxsys_getpid());
    uint16_t fcw = 0;
    uint32_t mxcsr = 0;
    __asm__ volatile("fnstcw %0" : "=m"(fcw));
    __asm__ volatile("stmxcsr %0" : "=m"(mxcsr));
    CHECK(fcw == 0x037F);
    CHECK((mxcsr & 0xFFC0) == 0x1F80);
    printf("[LIBCTEST] %s, kernel ABI flags 0x%04x (FILEIO %s)\n", nxc_version(),
           (unsigned)nxsys_abi_flags(), nxsys_abi_has(NXSYS_ABI_FLAG_FILEIO) ? "yes" : "no");
    section("start", f0);
}

/* ---- formatting and conversion ---- */
static void test_format(void) {
    int f0 = g_fail;
    char b[128];
    snprintf(b, sizeof b, "%d|%5s|%-4x|%08.3f|%e|%g|%a", -42, "ab", 255u, 3.14159, 1e-10, 100000.0, 1.0);
    CHECK(strcmp(b, "-42|   ab|ff  |0003.142|1.000000e-10|100000|0x1p+0") == 0);
    snprintf(b, sizeof b, "%.20f", 0.1);
    CHECK(strcmp(b, "0.10000000000000000555") == 0);
    snprintf(b, sizeof b, "%.17g", 1.0 / 3.0);
    CHECK(strcmp(b, "0.33333333333333331") == 0);
    CHECK(snprintf(NULL, 0, "%lld", -9223372036854775807LL - 1) == 20);
    /* round trips through printf and strtod */
    uint64_t seed = 0x9E3779B97F4A7C15ull;
    int bad = 0;
    for (int i = 0; i < 3000; i++) {
        seed ^= seed >> 12; seed ^= seed << 25; seed ^= seed >> 27;
        uint64_t u = seed * 0x2545F4914F6CDD1Dull;
        double d;
        memcpy(&d, &u, sizeof d);
        if (isnan(d) || isinf(d)) continue;
        snprintf(b, sizeof b, "%.17g", d);
        double back = strtod(b, NULL);
        if (memcmp(&back, &d, sizeof d) != 0) bad++;
    }
    CHECK(bad == 0);
    int n = 0;
    char w[16];
    CHECK(sscanf("x=12 name", "x=%d %15s", &n, w) == 2 && n == 12 && strcmp(w, "name") == 0);
    section("format", f0);
}

/* ---- math, run on the target FPU ---- */
static void test_math(void) {
    int f0 = g_fail;
    CHECK(sin(1.0) == 0x1.aed548f090ceep-1);
    CHECK(exp(1.0) == 0x1.5bf0a8b145769p+1);
    CHECK(log(10.0) == 0x1.26bb1bbb55516p+1);
    CHECK(pow(2.0, 0.5) == 0x1.6a09e667f3bcdp+0);
    CHECK(atan2(1.0, 1.0) == 0x1.921fb54442d18p-1);
    CHECK(sqrt(2.0) == 0x1.6a09e667f3bcdp+0);
    CHECK(fmod(10.5, 3.0) == 1.5);
    errno = 0;
    CHECK(isnan(log(-1.0)) && errno == EDOM);
    section("math", f0);
}

/* ---- allocator on the kernel's brk ---- */
#define SLOTS 512
static unsigned char *g_p[SLOTS];
static size_t g_n[SLOTS];

static void test_malloc(void) {
    int f0 = g_fail;
    uint64_t seed = 12345;
    int bad = 0, oom = 0;
    for (int it = 0; it < 30000; it++) {
        seed = seed * 6364136223846793005ull + 1442695040888963407ull;
        int i = (int)((seed >> 33) % SLOTS);
        if (g_p[i]) {
            for (size_t k = 0; k < g_n[i]; k++) if (g_p[i][k] != (unsigned char)(i + k)) { bad++; break; }
            free(g_p[i]);
            g_p[i] = NULL;
        } else {
            size_t n = (size_t)((seed >> 20) % ((it & 15) ? 2000 : 200000));
            g_p[i] = malloc(n);
            if (!g_p[i]) { oom++; continue; }
            g_n[i] = n;
            for (size_t k = 0; k < n; k++) g_p[i][k] = (unsigned char)(i + k);
        }
    }
    CHECK(bad == 0);
    CHECK(oom == 0);
    CHECK(nxc_heap_check() == 0);
    nxc_heap_stats_t st;
    nxc_heap_stats(&st);
    printf("[LIBCTEST] heap: %zu KB mapped, peak %zu KB, %zu blocks in use\n", st.heap_bytes / 1024,
           st.peak_heap_bytes / 1024, st.used_blocks);
    for (int i = 0; i < SLOTS; i++) { free(g_p[i]); g_p[i] = NULL; }
    CHECK(nxc_heap_check() == 0);
    nxc_heap_stats(&st);
    CHECK(st.used_blocks == 0);
    void *big = malloc(8u << 20);
    CHECK(big != NULL);
    if (big) memset(big, 0x5A, 8u << 20);
    free(big);
    nxc_heap_stats(&st);
    printf("[LIBCTEST] heap after free: %zu KB mapped (8 MB block given back)\n", st.heap_bytes / 1024);
    CHECK(st.heap_bytes <= 256u * 1024u);
    section("malloc", f0);
}

/* ---- stdio over the VFS ---- */
static int pick_dir(char *out, size_t cap) {
    static const char *const dirs[] = { "/tmp", "/Users/Shared", "/home" };
    for (size_t i = 0; i < sizeof dirs / sizeof dirs[0]; i++) {
        snprintf(out, cap, "%s/libctest.tmp", dirs[i]);
        FILE *f = fopen(out, "w");
        if (f) {
            fclose(f);
            return 0;
        }
        printf("[LIBCTEST] cannot create %s: %s\n", out, strerror(errno));
    }
    return -1;
}

static void test_files(const char *self) {
    int f0 = g_fail;
    /* read our own program file: an ELF header */
    FILE *f = fopen(self, "rb");
    CHECK(f != NULL);
    if (f) {
        unsigned char hdr[4] = { 0 };
        CHECK(fread(hdr, 1, 4, f) == 4);
        CHECK(memcmp(hdr, "\x7f" "ELF", 4) == 0);
        CHECK(fseek(f, 0, SEEK_END) == 0);
        long size = ftell(f);
        CHECK(size > 1000);
        printf("[LIBCTEST] %s is %ld bytes\n", self, size);
        fclose(f);
    }
    errno = 0;
    CHECK(fopen("/apps/no-such-file", "r") == NULL && errno == ENOENT);

    char path[64], path2[80];
    if (pick_dir(path, sizeof path) != 0) {
        CHECK(!"no writable directory");
        section("files", f0);
        return;
    }
    f = fopen(path, "w");
    CHECK(f != NULL);
    if (!f) { section("files", f0); return; }
    for (int i = 0; i < 500; i++) fprintf(f, "line %03d %.3f\n", i, i * 0.5);
    long written = ftell(f);
    CHECK(fclose(f) == 0);

    f = fopen(path, "a");
    CHECK(f != NULL);
    if (f) { fputs("tail\n", f); fclose(f); }

    f = fopen(path, "r");
    CHECK(f != NULL);
    if (f) {
        char line[64];
        int i = 0, ok = 1;
        while (i < 500 && fgets(line, sizeof line, f)) {
            int n = -1;
            double v = -1;
            if (sscanf(line, "line %d %lf", &n, &v) != 2 || n != i || v != i * 0.5) ok = 0;
            i++;
        }
        CHECK(ok && i == 500);
        CHECK(ftell(f) == written);
        CHECK(fgets(line, sizeof line, f) && strcmp(line, "tail\n") == 0);
        CHECK(fgets(line, sizeof line, f) == NULL && feof(f));
        CHECK(fseek(f, 9, SEEK_SET) == 0 && fgetc(f) == '0');
        fclose(f);
        printf("[LIBCTEST] %s: wrote %ld bytes, read back ok\n", path, written + 5);
    }
    snprintf(path2, sizeof path2, "%s.2", path);
    remove(path2);
    CHECK(rename(path, path2) == 0);
    CHECK(remove(path2) == 0);
    errno = 0;
    CHECK(remove(path2) == -1 && errno == ENOENT);
    section("files", f0);
}

/* ---- clocks ---- */
static void test_time(void) {
    int f0 = g_fail;
    time_t now = time(NULL);
    struct timespec ts;
    if (now == (time_t)-1) {
        /* nxlibc's answer when the kernel clock is not set: not a test
         * failure of the library, but worth seeing in the log */
        CHECK(timespec_get(&ts, TIME_UTC) == 0);
        printf("[LIBCTEST] WARN: no calendar time (kernel RTC not set); time() = -1 as ISO C asks\n");
    } else {
        struct tm tm;
        char when[64];
        CHECK(gmtime_r(&now, &tm) != NULL);
        strftime(when, sizeof when, "%Y-%m-%d %H:%M:%S UTC", &tm);
        printf("[LIBCTEST] RTC time %s (%lld)\n", when, (long long)now);
        CHECK(now > 1577836800);      /* after 2020-01-01 */
        CHECK(timespec_get(&ts, TIME_UTC) == TIME_UTC && ts.tv_sec >= now);
    }
    clock_t c0 = clock();
    uint64_t t0 = nxsys_time();
    nxsys_sleep(50);
    clock_t c1 = clock();
    CHECK(nxsys_time() - t0 >= 40);
    CHECK(c1 - c0 >= 40 * CLOCKS_PER_SEC / 1000);
    section("time", f0);
}

/* ---- control flow ---- */
static jmp_buf g_jb;
static void deep(int n);
static void (*volatile g_deep)(int) = deep;   /* keeps the recursion real */
static void deep(int n) {
    if (n == 0) longjmp(g_jb, 7);
    g_deep(n - 1);
}
static int cmp(const void *a, const void *b) { return *(const int *)a - *(const int *)b; }

static void test_misc(void) {
    int f0 = g_fail;
    volatile int marker = 1;
    int r = setjmp(g_jb);
    if (r == 0) { marker = 2; deep(50); }
    CHECK(r == 7 && marker == 2);
    int v[200];
    for (int i = 0; i < 200; i++) v[i] = (i * 7919) % 200;
    qsort(v, 200, sizeof v[0], cmp);
    int sorted = 1;
    for (int i = 0; i < 200; i++) if (v[i] != i) sorted = 0;
    CHECK(sorted);
    CHECK(strcmp(strerror(ENOENT), "No such file or directory") == 0);
    /* stdin has no source today: EOF, not a hang */
    CHECK(getchar() == EOF);
    section("misc", f0);
}

int main(int argc, char **argv) {
    printf("[LIBCTEST] nxlibc test starting\n");
    test_startup(argc, argv);
    test_format();
    test_math();
    test_malloc();
    test_files((argc > 0 && argv[0] && argv[0][0] == '/') ? argv[0] : "/apps/libctest");
    test_time();
    test_misc();
    printf("[LIBCTEST] %d checks, %d failed\n", g_checks, g_fail);
    printf("[LIBCTEST] RESULT: %s\n", g_fail ? "FAIL" : "PASS");
    return g_fail ? 1 : 0;
}
