/*
 * NeolyxOS fdtest - per-process file descriptor isolation test
 *
 * Checks that file descriptors belong to one process:
 *   1. A new process starts with an empty table: its first open gives fd 0
 *      even while other processes (the desktop) hold files open.
 *   2. A child cannot read or close the parent's descriptors (-EBADF), and
 *      the parent's descriptor still works after the child has tried.
 *   3. Files a process leaves open are closed when it exits: children that
 *      each leak 9 files are spawned more times than the kernel's global
 *      open-file table (64) could hold without that cleanup.
 *   4. A process gets exactly PROCESS_FD_MAX (16) descriptors, then -EMFILE.
 *
 * Results go to the serial log as "[FDTEST] PASS ..." / "[FDTEST] FAIL ...",
 * ending with one "[FDTEST] RESULT: PASS" or "[FDTEST] RESULT: FAIL" line.
 * The exit code is 0 on success, 1 on failure.
 *
 * Launch from the desktop's app drawer (/apps/fdtest on the boot disk) or
 * with sys_spawn("/apps/fdtest", ...). Run as "fdtest child" it acts as the
 * child half of the test.
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>

/* ============ Syscalls (numbers from kernel/include/core/syscall.h) ============ */

#define SYS_EXIT        1
#define SYS_GETPID      5
#define SYS_WAIT        9
#define SYS_SPAWN       13
#define SYS_OPEN        20
#define SYS_CLOSE       21
#define SYS_READ        22
#define SYS_DEBUG_PRINT 127

#define SPAWN_WAITABLE  0x1
#define VFS_O_RDONLY    0x0001

#define EBADF           9
#define EMFILE          24

#define FD_MAX          16      /* PROCESS_FD_MAX in kernel/include/core/process.h */
#define CHILD_LEAKS     9       /* files each child leaves open on purpose */
#define LEAK_ROUNDS     10      /* 10 x 9 = 90 > 64 global VFS open files */

static const char *const SELF_PATH = "/apps/fdtest";

static inline int64_t sc1(int64_t num, int64_t a1) {
    int64_t ret;
    __asm__ volatile("syscall" : "=a"(ret) : "a"(num), "D"(a1) : "rcx", "r11", "memory");
    return ret;
}

static inline int64_t sc2(int64_t num, int64_t a1, int64_t a2) {
    int64_t ret;
    __asm__ volatile("syscall" : "=a"(ret) : "a"(num), "D"(a1), "S"(a2)
                     : "rcx", "r11", "memory");
    return ret;
}

static inline int64_t sc3(int64_t num, int64_t a1, int64_t a2, int64_t a3) {
    int64_t ret;
    __asm__ volatile("syscall" : "=a"(ret) : "a"(num), "D"(a1), "S"(a2), "d"(a3)
                     : "rcx", "r11", "memory");
    return ret;
}

/* ============ Output ============ */

static char g_line[160];
static int g_len;

static void out_str(const char *s) {
    while (*s && g_len < (int)sizeof(g_line) - 1) g_line[g_len++] = *s++;
}

static void out_sdec(int64_t v) {
    char buf[22];
    int i = 21;
    int neg = v < 0;
    uint64_t u = neg ? (uint64_t)(-(v + 1)) + 1 : (uint64_t)v;
    buf[i] = '\0';
    if (u == 0) buf[--i] = '0';
    while (u > 0 && i > 1) { buf[--i] = (char)('0' + u % 10); u /= 10; }
    if (neg) buf[--i] = '-';
    out_str(&buf[i]);
}

static void flush(void) {
    g_line[g_len] = '\0';
    sc1(SYS_DEBUG_PRINT, (int64_t)g_line);
    g_len = 0;
}

static void finish(int code) {
    sc1(SYS_EXIT, code);
    for (;;) { __asm__ volatile("pause"); }
}

/* ============ Helpers ============ */

static int g_failed;

/* Log one check; a failed check marks the whole run as failed */
static void check(int ok, const char *what, int64_t got) {
    out_str(ok ? "[FDTEST] PASS " : "[FDTEST] FAIL ");
    out_str(what);
    out_str(" (got ");
    out_sdec(got);
    out_str(")\n");
    flush();
    if (!ok) g_failed = 1;
}

static int64_t open_self(void) {
    return sc3(SYS_OPEN, (int64_t)SELF_PATH, VFS_O_RDONLY, 0);
}

/* Read 4 bytes from fd into out; returns the syscall result */
static int64_t read4(int64_t fd, uint8_t out[4]) {
    return sc3(SYS_READ, fd, (int64_t)out, 4);
}

static int is_elf_magic(const uint8_t b[4]) {
    return b[0] == 0x7F && b[1] == 'E' && b[2] == 'L' && b[3] == 'F';
}

/* Spawn "fdtest child" and wait for it; returns its exit status, or -1000
 * minus the error if it could not be spawned or waited for */
static int64_t run_child(void) {
    const char *argv[3] = { "fdtest", "child", 0 };
    int64_t pid = sc3(SYS_SPAWN, (int64_t)SELF_PATH, (int64_t)argv, SPAWN_WAITABLE);
    if (pid <= 0) return -1000 + pid;

    int status = -1;
    int64_t waited = sc2(SYS_WAIT, pid, (int64_t)&status);
    if (waited != pid) return -2000 + waited;
    return status;
}

static int streq(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

/* ============ Child half ============ */

/*
 * The parent holds fd 0 open. A separate table means fd 0 is not open
 * here, and the child's own first open gets fd 0. The child then leaks
 * CHILD_LEAKS files on purpose and exits: the kernel must close them.
 * Exit code: 0 if every check held, otherwise the number of the first
 * check that failed.
 */
static void child_main(void) {
    uint8_t buf[4];

    if (sc3(SYS_READ, 0, (int64_t)buf, 4) != -EBADF) finish(10);
    if (sc1(SYS_CLOSE, 0) != -EBADF) finish(11);

    int64_t fd = open_self();
    if (fd != 0) finish(12);
    if (read4(fd, buf) != 4 || !is_elf_magic(buf)) finish(13);

    for (int i = 1; i < CHILD_LEAKS; i++) {
        if (open_self() != i) finish(14);
    }
    finish(0);  /* leave all CHILD_LEAKS files open */
}

/* ============ Parent half ============ */

void _start(long argc, char **argv) {
    if (argc >= 2 && argv && argv[1] && streq(argv[1], "child")) {
        child_main();
    }

    out_str("[FDTEST] PID ");
    out_sdec(sc1(SYS_GETPID, 0));
    out_str(" started\n");
    flush();

    /* 1. Fresh table: first descriptor is 0 */
    uint8_t buf[4];
    int64_t fd = open_self();
    check(fd == 0, "first open in a new process gets fd 0", fd);
    if (fd < 0) {
        out_str("[FDTEST] RESULT: FAIL (cannot open ");
        out_str(SELF_PATH);
        out_str(")\n");
        flush();
        finish(1);
    }
    int64_t r = read4(fd, buf);
    check(r == 4 && is_elf_magic(buf), "parent reads its own fd", r);

    /* 2. Child cannot touch the parent's fd; parent's fd survives */
    int64_t status = run_child();
    check(status == 0, "child sees an empty table and gets its own fd 0", status);
    r = read4(fd, buf);
    check(r == 4, "parent fd still open after the child closed fd 0", r);

    /* 3. Exit closes leaked files: 10 x 9 leaked files > 64 global slots */
    int leak_ok = 1;
    int64_t leak_status = 0;
    for (int round = 0; round < LEAK_ROUNDS && leak_ok; round++) {
        leak_status = run_child();
        if (leak_status != 0) leak_ok = 0;
    }
    check(leak_ok, "files leaked by 10 exited children were all closed", leak_status);

    /* 4. Exactly FD_MAX descriptors per process, then -EMFILE */
    int64_t opened[FD_MAX];
    int count = 0;
    int64_t last = 0;
    for (int i = 1; i < FD_MAX + 1; i++) {
        last = open_self();
        if (last < 0) break;
        opened[count++] = last;
    }
    check(count == FD_MAX - 1, "process holds 16 descriptors (1 + 15 more)", count + 1);
    check(last == -EMFILE, "17th open returns -EMFILE", last);
    for (int i = 0; i < count; i++) {
        if (sc1(SYS_CLOSE, opened[i]) != 0) {
            check(0, "close of a held descriptor", opened[i]);
        }
    }
    r = sc1(SYS_CLOSE, fd);
    check(r == 0, "close parent fd", r);
    r = sc1(SYS_CLOSE, fd);
    check(r == -EBADF, "second close of the same fd returns -EBADF", r);

    out_str(g_failed ? "[FDTEST] RESULT: FAIL\n" : "[FDTEST] RESULT: PASS\n");
    flush();
    finish(g_failed ? 1 : 0);
}
