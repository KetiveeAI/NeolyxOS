/*
 * NeolyxOS fdtest - file descriptors and file syscalls
 *
 * Spec: docs/kernel/proposals/beta-process-abi.md, sections 4 and 6.
 *
 * Checks:
 *   1. Descriptors are per process and 0-2 are never handed out by open:
 *      a new process's first open gives fd 3 while fds 0-2 stay empty.
 *   2. A child cannot read or close the parent's descriptors (-EBADF), and
 *      the parent's descriptor still works after the child has tried.
 *   3. Files a process leaves open are closed when it exits: children that
 *      each leak 60 files are spawned more times than the kernel's global
 *      open-file table (1024) could hold without that cleanup.
 *   4. A process gets exactly 61 descriptors from open (3-63), then -EMFILE.
 *   5. dup shares the open file (and its offset); dup2 places a file at a
 *      chosen fd, including 0-2, and replaces what was there.
 *   6. fd.ctl: close-on-exec flag, status flags, fd type.
 *   7. fstat matches stat; a directory fd reads as -EISDIR.
 *   8. Errors are -errno: ENOENT, EINVAL, EISDIR, ENOTDIR, EEXIST, EACCES
 *      (open of a directory for writing is checked in a /tmp directory).
 *   9. Working directory per process: chdir/getcwd, relative paths, and a
 *      spawned child starts in its parent's directory.
 *  10. Files in /tmp: create, O_EXCL, write/read, append, unlink refused
 *      while open (-EBUSY), unlink, relative names, rename.
 *  11. exec closes fds marked close-on-exec and keeps the others.
 *
 * Needs /apps/fdtest (this program) and a /tmp directory on the boot disk.
 *
 * Results go to the serial log as "[FDTEST] PASS ..." / "[FDTEST] FAIL ...",
 * ending with one "[FDTEST] RESULT: PASS" or "[FDTEST] RESULT: FAIL" line.
 * The exit code is 0 on success, 1 on failure. Run as "fdtest child",
 * "fdtest cwdchild", "fdtest exec1" or "fdtest exec2" it acts as one of the
 * helper processes.
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>

/* ============ Syscalls (numbers from kernel/include/core/syscall.h) ============ */

#define SYS_EXIT        1
#define SYS_GETPID      5
#define SYS_EXEC        8
#define SYS_WAIT        9
#define SYS_SPAWN       13
#define SYS_DUP         14
#define SYS_DUP2        15
#define SYS_FD_CTL      18
#define SYS_OPEN        20
#define SYS_CLOSE       21
#define SYS_READ        22
#define SYS_WRITE       23
#define SYS_SEEK        24
#define SYS_STAT        25
#define SYS_FSTAT       26
#define SYS_UNLINK      27
#define SYS_MKDIR       31
#define SYS_RMDIR       32
#define SYS_CHDIR       33
#define SYS_GETCWD      34
#define SYS_RENAME      35
#define SYS_DEBUG_PRINT 127

#define SPAWN_WAITABLE  0x1

/* core/nx_file.h */
#define O_RDONLY        0x0001
#define O_WRONLY        0x0002
#define O_CREATE        0x0100
#define O_TRUNC         0x0200
#define O_APPEND        0x0400
#define O_EXCL          0x0800
#define O_CLOEXEC       0x1000
#define O_NONBLOCK      0x2000
#define FD_GET_FLAGS    1
#define FD_SET_FLAGS    2
#define FD_GET_STATUS   3
#define FD_SET_STATUS   4
#define FD_GET_TYPE     5
#define FD_CLOEXEC      0x1
#define FD_TYPE_FILE    1
#define FD_TYPE_DIR     2
#define S_IFMT          0170000
#define S_IFDIR         0040000
#define S_IFREG         0100000

#define ENOENT          2
#define EBADF           9
#define EACCES          13
#define EBUSY           16
#define EEXIST          17
#define ENOTDIR         20
#define EISDIR          21
#define EINVAL          22
#define EMFILE          24

#define FD_MAX          64      /* NX_FD_MAX */
#define FD_FIRST        3       /* NX_FD_FIRST_AUTO */
#define CHILD_LEAKS     60      /* files each child leaves open on purpose */
#define LEAK_ROUNDS     18      /* 18 x 60 = 1080 > 1024 global VFS open files */

typedef struct {
    uint64_t st_ino;
    uint32_t st_mode;
    uint32_t st_nlink;
    uint32_t st_uid;
    uint32_t st_gid;
    uint64_t st_size;
    uint64_t st_atime;
    uint64_t st_mtime;
    uint64_t st_ctime;
} stat_t;

static const char *const SELF_PATH = "/apps/fdtest";
static const char *const TMP_FILE = "/tmp/fdtest.txt";
static const char *const TMP_DIR  = "/tmp/fdtest.d";

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

static int streq(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static int64_t open_path(const char *p, int64_t flags) {
    return sc3(SYS_OPEN, (int64_t)p, flags, 0);
}

static int64_t open_self(void) {
    return open_path(SELF_PATH, O_RDONLY);
}

static int64_t read_n(int64_t fd, void *out, int64_t n) {
    return sc3(SYS_READ, fd, (int64_t)out, n);
}

static int64_t write_s(int64_t fd, const char *s) {
    int64_t n = 0;
    while (s[n]) n++;
    return sc3(SYS_WRITE, fd, (int64_t)s, n);
}

static int is_elf_magic(const uint8_t b[4]) {
    return b[0] == 0x7F && b[1] == 'E' && b[2] == 'L' && b[3] == 'F';
}

static int64_t stat_size(const char *p) {
    stat_t st;
    int64_t r = sc2(SYS_STAT, (int64_t)p, (int64_t)&st);
    return r < 0 ? r : (int64_t)st.st_size;
}

/* Spawn this program with one argument and wait; returns its exit status,
 * or -1000 + error when it could not be spawned or waited for */
static int64_t run_helper(const char *path, const char *mode) {
    const char *argv[3] = { "fdtest", mode, 0 };
    int64_t pid = sc3(SYS_SPAWN, (int64_t)path, (int64_t)argv, SPAWN_WAITABLE);
    if (pid <= 0) return -1000 + pid;

    int status = -1;
    int64_t waited = sc2(SYS_WAIT, pid, (int64_t)&status);
    if (waited != pid) return -2000 + waited;
    return status;
}

/* ============ Helper processes ============ */

/*
 * "child": the parent holds fd 3. A separate table means fd 3 is not open
 * here, and the child's own first open gets fd 3. Then it leaks
 * CHILD_LEAKS files on purpose and exits: the kernel must close them.
 * Exit code: 0 if every check held, otherwise the number of the first
 * check that failed.
 */
static void child_main(void) {
    uint8_t buf[4];

    if (read_n(FD_FIRST, buf, 4) != -EBADF) finish(10);
    if (sc1(SYS_CLOSE, FD_FIRST) != -EBADF) finish(11);
    if (read_n(0, buf, 4) != -EBADF) finish(12);

    int64_t fd = open_self();
    if (fd != FD_FIRST) finish(13);
    if (read_n(fd, buf, 4) != 4 || !is_elf_magic(buf)) finish(14);

    for (int i = 1; i < CHILD_LEAKS; i++) {
        if (open_self() != FD_FIRST + i) finish(15);
    }
    finish(0);  /* leave all CHILD_LEAKS files open */
}

/* "cwdchild": started from /apps with a relative path; must be in /apps */
static void cwdchild_main(void) {
    char cwd[64];
    if (sc2(SYS_GETCWD, (int64_t)cwd, sizeof(cwd)) != 5) finish(20);
    if (!streq(cwd, "/apps")) finish(21);
    int64_t fd = open_path("fdtest", O_RDONLY);         /* relative to /apps */
    if (fd != FD_FIRST) finish(22);
    finish(0);
}

/* "exec1": open one fd with close-on-exec and one without, then exec */
static void exec1_main(void) {
    int64_t keep = open_self();                          /* fd 3 */
    int64_t drop = open_path(SELF_PATH, O_RDONLY | O_CLOEXEC);  /* fd 4 */
    if (keep != 3 || drop != 4) finish(30);
    const char *argv[3] = { "fdtest", "exec2", 0 };
    int64_t r = sc3(SYS_EXEC, (int64_t)SELF_PATH, (int64_t)argv, 0);
    (void)r;
    finish(31);                                          /* exec failed */
}

/* "exec2": after exec, fd 3 is still open and fd 4 is closed */
static void exec2_main(void) {
    if (sc3(SYS_FD_CTL, 3, FD_GET_TYPE, 0) != FD_TYPE_FILE) finish(32);
    if (sc3(SYS_FD_CTL, 4, FD_GET_TYPE, 0) != -EBADF) finish(33);
    finish(0);
}

/* ============ Parent sections ============ */

static void test_dup(int64_t fd) {
    uint8_t a[4], b[4];
    int64_t r = sc3(SYS_SEEK, fd, 0, 0);
    check(r == 0, "seek to start", r);

    int64_t d = sc1(SYS_DUP, fd);
    check(d == FD_FIRST + 1, "dup returns the lowest free fd from 3", d);
    r = read_n(fd, a, 4);
    int64_t r2 = read_n(d, b, 4);
    /* ELF bytes 4-7 of an x86_64 program: class 2, data 1, version 1, ABI 0 */
    check(r == 4 && is_elf_magic(a) && r2 == 4 && b[0] == 2 && b[1] == 1 && b[2] == 1,
          "dup shares the file offset", r2);
    check(sc1(SYS_CLOSE, d) == 0, "close the dup", 0);
    r = read_n(fd, a, 4);
    check(r == 4, "original fd works after its dup is closed", r);

    r = sc2(SYS_DUP2, fd, 1);
    check(r == 1, "dup2 can place a file at fd 1", r);
    r = sc3(SYS_FD_CTL, 1, FD_GET_TYPE, 0);
    check(r == FD_TYPE_FILE, "fd 1 now holds the file", r);
    int64_t other = open_self();
    r = sc2(SYS_DUP2, other, 1);
    check(r == 1, "dup2 onto an open fd replaces it", r);
    check(sc1(SYS_CLOSE, other) == 0 && sc3(SYS_SEEK, 1, 0, 0) == 0 && read_n(1, a, 4) == 4 &&
          is_elf_magic(a), "replacement survives closing its source", 0);
    check(sc1(SYS_CLOSE, 1) == 0, "close fd 1", 0);
    r = sc2(SYS_DUP2, fd, fd);
    check(r == fd, "dup2 of an fd onto itself returns it", r);
    r = sc2(SYS_DUP2, fd, FD_MAX);
    check(r == -EBADF, "dup2 to fd 64 is -EBADF", r);
    r = sc2(SYS_DUP2, 50, 5);
    check(r == -EBADF, "dup2 of a closed fd is -EBADF", r);
    r = sc1(SYS_DUP, 99);
    check(r == -EBADF, "dup of fd 99 is -EBADF", r);
}

static void test_fd_ctl(int64_t fd) {
    int64_t r = sc3(SYS_FD_CTL, fd, FD_GET_FLAGS, 0);
    check(r == 0, "new fd has no fd flags", r);
    r = sc3(SYS_FD_CTL, fd, FD_SET_FLAGS, FD_CLOEXEC);
    int64_t g = sc3(SYS_FD_CTL, fd, FD_GET_FLAGS, 0);
    check(r == 0 && g == FD_CLOEXEC, "set close-on-exec", g);
    sc3(SYS_FD_CTL, fd, FD_SET_FLAGS, 0);
    r = sc3(SYS_FD_CTL, fd, FD_SET_FLAGS, 2);
    check(r == -EINVAL, "unknown fd flag is -EINVAL", r);

    r = sc3(SYS_FD_CTL, fd, FD_GET_STATUS, 0);
    check(r == O_RDONLY, "status is the access mode", r);
    r = sc3(SYS_FD_CTL, fd, FD_SET_STATUS, O_RDONLY | O_NONBLOCK);
    g = sc3(SYS_FD_CTL, fd, FD_GET_STATUS, 0);
    check(r == 0 && g == (O_RDONLY | O_NONBLOCK), "set non-blocking", g);
    sc3(SYS_FD_CTL, fd, FD_SET_STATUS, 0);
    r = sc3(SYS_FD_CTL, fd, FD_SET_STATUS, O_WRONLY);
    check(r == -EINVAL, "access mode cannot change", r);
    r = sc3(SYS_FD_CTL, fd, 99, 0);
    check(r == -EINVAL, "unknown command is -EINVAL", r);
    r = sc3(SYS_FD_CTL, fd, FD_GET_TYPE, 0);
    check(r == FD_TYPE_FILE, "type of a file fd", r);

    int64_t dfd = open_path("/apps", O_RDONLY);
    r = sc3(SYS_FD_CTL, dfd, FD_GET_TYPE, 0);
    check(dfd >= FD_FIRST && r == FD_TYPE_DIR, "type of a directory fd", r);
    uint8_t b[4];
    r = read_n(dfd, b, 4);
    check(r == -EISDIR, "read from a directory fd is -EISDIR", r);
    stat_t st;
    r = sc2(SYS_FSTAT, dfd, (int64_t)&st);
    check(r == 0 && (st.st_mode & S_IFMT) == S_IFDIR, "fstat of a directory fd", st.st_mode);
    sc1(SYS_CLOSE, dfd);
}

static void test_fstat(int64_t fd) {
    stat_t st;
    int64_t r = sc2(SYS_FSTAT, fd, (int64_t)&st);
    int64_t sz = stat_size(SELF_PATH);
    check(r == 0 && (st.st_mode & S_IFMT) == S_IFREG && sz > 0 && (int64_t)st.st_size == sz,
          "fstat matches stat for a file", (int64_t)st.st_size);
    r = sc2(SYS_FSTAT, 60, (int64_t)&st);
    check(r == -EBADF, "fstat of a closed fd is -EBADF", r);
}

static void test_errors(void) {
    int64_t r = open_path("/no/such/file", O_RDONLY);
    check(r == -ENOENT, "open of a missing file is -ENOENT", r);
    r = open_path(SELF_PATH, 0x80000);
    check(r == -EINVAL, "open with an unknown flag is -EINVAL", r);
    r = open_path(SELF_PATH, 0);                          /* nximage opens this way */
    {
        uint8_t m[4] = {0};
        int64_t n = r >= 0 ? read_n(r, m, 4) : r;
        check(n == 4 && m[0] == 0x7F && m[1] == 'E', "open with access mode 0 reads", n);
        if (r >= 0) sc1(SYS_CLOSE, r);
    }
    r = open_path("/System/fdtest-denied", O_WRONLY | O_CREATE);
    check(r == -EACCES, "create in /System is -EACCES (SIP)", r);
    r = sc2(SYS_MKDIR, (int64_t)"/apps", 0755);
    check(r == -EEXIST, "mkdir of an existing directory is -EEXIST", r);
    r = sc1(SYS_RMDIR, (int64_t)SELF_PATH);
    check(r == -ENOTDIR, "rmdir of a file is -ENOTDIR", r);
    r = sc1(SYS_UNLINK, (int64_t)"/apps");
    check(r == -EISDIR, "unlink of a directory is -EISDIR", r);
    r = sc1(SYS_UNLINK, (int64_t)"/no/such/file");
    check(r == -ENOENT, "unlink of a missing file is -ENOENT", r);
    r = stat_size("/no/such/file");
    check(r == -ENOENT, "stat of a missing file is -ENOENT", r);
    r = sc1(SYS_CHDIR, (int64_t)SELF_PATH);
    check(r == -ENOTDIR, "chdir to a file is -ENOTDIR", r);
}

static void test_cwd(void) {
    char cwd[64];
    int64_t r = sc2(SYS_GETCWD, (int64_t)cwd, sizeof(cwd));
    check(r == 1 && streq(cwd, "/"), "new process starts in /", r);
    r = sc1(SYS_CHDIR, (int64_t)"apps");
    check(r == 0, "chdir with a relative path", r);
    r = sc2(SYS_GETCWD, (int64_t)cwd, sizeof(cwd));
    check(r == 5 && streq(cwd, "/apps"), "getcwd after chdir", r);
    r = sc2(SYS_GETCWD, (int64_t)cwd, 3);
    check(r == -34 || r == -40, "getcwd into a short buffer fails", r);   /* -ERANGE */
    int64_t fd = open_path("./fdtest", O_RDONLY);
    check(fd >= FD_FIRST, "open with a relative path", fd);
    if (fd >= 0) sc1(SYS_CLOSE, fd);
    r = stat_size("../apps/./fdtest");
    check(r > 0, "stat with .. and . in a relative path", r);

    r = run_helper("fdtest", "cwdchild");                 /* relative program path */
    check(r == 0, "spawned child starts in the parent's directory", r);

    r = sc1(SYS_CHDIR, (int64_t)"..");
    sc2(SYS_GETCWD, (int64_t)cwd, sizeof(cwd));
    check(r == 0 && streq(cwd, "/"), "chdir .. back to /", r);
}

static void test_tmp_files(void) {
    stat_t st;
    if (sc2(SYS_STAT, (int64_t)"/tmp", (int64_t)&st) != 0) {
        check(0, "/tmp exists on the boot disk (needed for file tests)", -ENOENT);
        return;
    }
    sc1(SYS_UNLINK, (int64_t)TMP_FILE);                   /* leftover from a crash */

    /* A directory SIP lets us write into (directories outside /tmp, and /tmp
     * itself, are refused with -EACCES before the type check). */
    sc1(SYS_RMDIR, (int64_t)TMP_DIR);                     /* leftover from a crash */
    int64_t r = sc2(SYS_MKDIR, (int64_t)TMP_DIR, 0755);
    check(r == 0, "mkdir in /tmp", r);
    r = open_path(TMP_DIR, O_WRONLY);
    check(r == -EISDIR, "open of a directory for writing is -EISDIR", r);
    if (r >= 0) sc1(SYS_CLOSE, r);
    r = sc1(SYS_UNLINK, (int64_t)TMP_DIR);
    check(r == -EISDIR, "unlink of a directory in /tmp is -EISDIR", r);
    r = sc1(SYS_RMDIR, (int64_t)TMP_DIR);
    check(r == 0, "rmdir of an empty directory", r);

    int64_t fd = open_path(TMP_FILE, O_WRONLY | O_CREATE | O_TRUNC);
    check(fd >= FD_FIRST, "create a file in /tmp", fd);
    if (fd < 0) return;
    r = write_s(fd, "hello");
    check(r == 5, "write 5 bytes", r);
    uint8_t b[8];
    r = read_n(fd, b, 1);
    check(r == -EBADF, "read from a write-only fd is -EBADF", r);
    r = sc1(SYS_UNLINK, (int64_t)TMP_FILE);
    check(r == -EBUSY, "unlink of an open file is -EBUSY", r);
    sc1(SYS_CLOSE, fd);

    r = open_path(TMP_FILE, O_WRONLY | O_CREATE | O_EXCL);
    check(r == -EEXIST, "O_EXCL on an existing file is -EEXIST", r);

    fd = open_path(TMP_FILE, O_WRONLY | O_APPEND);
    r = write_s(fd, "!!");
    sc1(SYS_CLOSE, fd);
    check(r == 2 && stat_size(TMP_FILE) == 7, "append writes at the end", stat_size(TMP_FILE));

    fd = open_path(TMP_FILE, O_RDONLY);
    r = read_n(fd, b, 8);
    check(r == 7 && b[0] == 'h' && b[4] == 'o' && b[5] == '!' && b[6] == '!', "read back", r);
    r = write_s(fd, "x");
    check(r == -EBADF, "write to a read-only fd is -EBADF", r);
    sc1(SYS_CLOSE, fd);

    r = sc1(SYS_CHDIR, (int64_t)"/tmp");
    r = sc2(SYS_RENAME, (int64_t)"fdtest.txt", (int64_t)"fdtest2.txt");
    check(r == 0 && stat_size("/tmp/fdtest2.txt") == 7, "rename with relative names", r);
    r = sc1(SYS_UNLINK, (int64_t)"fdtest2.txt");
    check(r == 0, "unlink a closed file (relative name)", r);
    r = stat_size("/tmp/fdtest2.txt");
    check(r == -ENOENT, "unlinked file is gone", r);
    sc1(SYS_CHDIR, (int64_t)"/");
}

/* ============ Parent ============ */

void _start(long argc, char **argv) {
    if (argc >= 2 && argv && argv[1]) {
        if (streq(argv[1], "child")) child_main();
        if (streq(argv[1], "cwdchild")) cwdchild_main();
        if (streq(argv[1], "exec1")) exec1_main();
        if (streq(argv[1], "exec2")) exec2_main();
    }

    out_str("[FDTEST] PID ");
    out_sdec(sc1(SYS_GETPID, 0));
    out_str(" started\n");
    flush();

    /* 1. Fresh table: 0-2 empty, first open is fd 3 */
    uint8_t buf[4];
    int64_t r = read_n(0, buf, 4);
    check(r == -EBADF, "fd 0 is empty in a spawned process", r);
    r = sc1(SYS_CLOSE, 2);
    check(r == -EBADF, "fd 2 is empty in a spawned process", r);
    int64_t fd = open_self();
    check(fd == FD_FIRST, "first open in a new process gets fd 3", fd);
    if (fd < 0) {
        out_str("[FDTEST] RESULT: FAIL (cannot open ");
        out_str(SELF_PATH);
        out_str(")\n");
        flush();
        finish(1);
    }
    r = read_n(fd, buf, 4);
    check(r == 4 && is_elf_magic(buf), "parent reads its own fd", r);

    /* 2. Child cannot touch the parent's fd; parent's fd survives */
    int64_t status = run_helper(SELF_PATH, "child");
    check(status == 0, "child sees an empty table and gets its own fd 3", status);
    r = read_n(fd, buf, 4);
    check(r == 4, "parent fd still open after the child closed fd 3", r);

    /* 3. Exit closes leaked files: 18 x 60 leaked files > 1024 global slots */
    int leak_ok = 1;
    int64_t leak_status = 0;
    for (int round = 0; round < LEAK_ROUNDS && leak_ok; round++) {
        leak_status = run_helper(SELF_PATH, "child");
        if (leak_status != 0) leak_ok = 0;
    }
    check(leak_ok, "files leaked by 18 exited children were all closed", leak_status);

    /* 4. Exactly 61 descriptors from open (3-63), then -EMFILE */
    int64_t opened[FD_MAX];
    int count = 0;
    int64_t last = 0;
    for (int i = 0; i < FD_MAX; i++) {
        last = open_self();
        if (last < 0) break;
        opened[count++] = last;
    }
    check(count == FD_MAX - FD_FIRST - 1, "process holds 61 descriptors (fd 3 + 60 more)", count + 1);
    check(last == -EMFILE, "62nd open returns -EMFILE", last);
    r = sc1(SYS_DUP, fd);
    check(r == -EMFILE, "dup with a full table returns -EMFILE", r);
    for (int i = 0; i < count; i++) {
        if (sc1(SYS_CLOSE, opened[i]) != 0) {
            check(0, "close of a held descriptor", opened[i]);
        }
    }

    /* 5-8 */
    test_dup(fd);
    test_fd_ctl(fd);
    test_fstat(fd);
    test_errors();

    /* 9-10 */
    test_cwd();
    test_tmp_files();

    /* 11. exec keeps fds without close-on-exec and closes the others */
    status = run_helper(SELF_PATH, "exec1");
    check(status == 0, "exec closes close-on-exec fds and keeps the rest", status);

    r = sc1(SYS_CLOSE, fd);
    check(r == 0, "close parent fd", r);
    r = sc1(SYS_CLOSE, fd);
    check(r == -EBADF, "second close of the same fd returns -EBADF", r);

    out_str(g_failed ? "[FDTEST] RESULT: FAIL\n" : "[FDTEST] RESULT: PASS\n");
    flush();
    finish(g_failed ? 1 : 0);
}
