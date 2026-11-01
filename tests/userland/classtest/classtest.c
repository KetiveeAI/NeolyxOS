/*
 * NeolyxOS classtest - K1 syscalls (ABI 1.3)
 *
 * Checks proc.set_class (153), mem.pressure_open (154),
 * mem.pressure_read (155) and mem.stats (156) from an ordinary app:
 *   - abi.query reports ABI 1.3 and NX_ABI_FLAG_MEMCLASS;
 *   - every error path and permission rule of the four calls (an app
 *     cannot raise itself, cannot touch other apps or the desktop);
 *   - the pressure handle works with obj.wait and handle.dup/close;
 *   - mem.stats is consistent and counts this process's own pages;
 *   - CPU share: one FOREGROUND and two BACKGROUND spinners (children that
 *     set their own class) get about 4:1:1 of the CPU (2:1 per class).
 *
 *   /apps/classtest                  the checks above
 *   /apps/classtest spin fg|bg       a spinner child (exit code = kloops)
 *
 * Prints "[CLASSTEST] RESULT: PASS" or "RESULT: FAIL".
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>
#include "../../../kernel/include/core/nx_ipc.h"
#include "../../../kernel/include/core/nx_mem.h"
#include "../../../kernel/include/core/nx_abi.h"

/* ============ Syscalls (numbers from kernel/include/core/syscall.h) ============ */

#define SYS_ABI_QUERY          0
#define SYS_EXIT               1
#define SYS_SLEEP              2
#define SYS_TIME               3
#define SYS_GETPID             5
#define SYS_GETPPID            6
#define SYS_WAIT               9
#define SYS_BRK                10
#define SYS_SPAWN              13
#define SYS_DEBUG_PRINT        127
#define SYS_HANDLE_CLOSE       140
#define SYS_SHM_CREATE         141
#define SYS_OBJ_WAIT           147
#define SYS_HANDLE_DUP         149
#define SYS_PROC_SET_CLASS     153
#define SYS_MEM_PRESSURE_OPEN  154
#define SYS_MEM_PRESSURE_READ  155
#define SYS_MEM_STATS          156

#define SPAWN_WAITABLE  0x1

#define E_BADF    9
#define E_NOMEM   12
#define E_ACCES   13
#define E_FAULT   14
#define E_INVAL   22
#define E_MFILE   24
#define E_SRCH    3
#define E_TIMEDOUT 110

#define SPIN_MS   3000

static inline int64_t sc0(int64_t n) {
    int64_t r;
    __asm__ volatile("syscall" : "=a"(r) : "a"(n) : "rcx", "r11", "memory");
    return r;
}
static inline int64_t sc1(int64_t n, int64_t a1) {
    int64_t r;
    __asm__ volatile("syscall" : "=a"(r) : "a"(n), "D"(a1) : "rcx", "r11", "memory");
    return r;
}
static inline int64_t sc2(int64_t n, int64_t a1, int64_t a2) {
    int64_t r;
    __asm__ volatile("syscall" : "=a"(r) : "a"(n), "D"(a1), "S"(a2) : "rcx", "r11", "memory");
    return r;
}
static inline int64_t sc3(int64_t n, int64_t a1, int64_t a2, int64_t a3) {
    int64_t r;
    __asm__ volatile("syscall" : "=a"(r) : "a"(n), "D"(a1), "S"(a2), "d"(a3)
                     : "rcx", "r11", "memory");
    return r;
}

/* ============ Output ============ */

static char g_line[192];
static int g_len;
static int g_failed;

static void out_str(const char *s) {
    while (s && *s && g_len < (int)sizeof(g_line) - 1) g_line[g_len++] = *s++;
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

static void check(int ok, const char *what, int64_t got) {
    out_str(ok ? "[CLASSTEST] PASS " : "[CLASSTEST] FAIL ");
    out_str(what);
    out_str(" (got ");
    out_sdec(got);
    out_str(")\n");
    flush();
    if (!ok) g_failed = 1;
}

static int streq(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

/* ============ Spinner child ============ */

static void spin_main(const char *cls) {
    if (streq(cls, "bg")) {
        int64_t r = sc2(SYS_PROC_SET_CLASS, sc0(SYS_GETPID), NX_CLASS_BACKGROUND);
        if (r != 0) finish(-1);
    }
    /* Start together: wait for the next whole 100 ms */
    int64_t t0 = sc0(SYS_TIME);
    sc1(SYS_SLEEP, 100 - (t0 % 100) + 100);
    int64_t start = sc0(SYS_TIME);
    volatile uint64_t sink = 0;
    int64_t kloops = 0;
    while (sc0(SYS_TIME) - start < SPIN_MS) {
        for (int i = 0; i < 1000; i++) sink += (uint64_t)i;
        kloops++;
    }
    (void)sink;
    finish((int)(kloops > 0x7FFFFFFF ? 0x7FFFFFFF : kloops));
}

/* ============ Checks ============ */

static void test_abi(void) {
    nx_abi_info_t info;
    int64_t r = sc1(SYS_ABI_QUERY, (int64_t)&info);
    check(r == 0, "abi.query", r);
    check(info.major == 1 && info.minor >= 3, "ABI is 1.3 or later (minor)", info.minor);
    check((info.flags & NX_ABI_FLAG_MEMCLASS) != 0, "abi.query reports NX_ABI_FLAG_MEMCLASS", info.flags);
}

static void test_stats(void) {
    nx_mem_stats_t st;
    check(sc2(SYS_MEM_STATS, (int64_t)&st, 79) == -E_INVAL, "mem.stats with size 79 is -EINVAL",
          sc2(SYS_MEM_STATS, (int64_t)&st, 79));
    int64_t r = sc2(SYS_MEM_STATS, 0, sizeof(st));
    check(r == -E_FAULT, "mem.stats with a NULL buffer is -EFAULT", r);
    r = sc2(SYS_MEM_STATS, 0x10, sizeof(st));
    check(r == -E_FAULT, "mem.stats into kernel memory is -EFAULT", r);

    r = sc2(SYS_MEM_STATS, (int64_t)&st, sizeof(st));
    check(r == 0, "mem.stats", r);
    check(st.size == 80 && st.version == 1, "mem.stats v1, 80 bytes (size)", st.size);
    check(st.free_bytes > 0 && st.free_bytes < st.total_bytes, "free < total (free MB)",
          (int64_t)(st.free_bytes >> 20));
    check(st.reserve_bytes >= (32ULL << 20) && st.reserve_bytes <= (128ULL << 20),
          "reserve between 32 and 128 MB (MB)", (int64_t)(st.reserve_bytes >> 20));
    check(st.level <= NX_MEM_CRITICAL, "level is a valid NX_MEM_* value", st.level);
    check(st.self_bytes > 0 && st.self_bytes <= st.user_bytes, "own pages counted in user pages (KB)",
          (int64_t)(st.self_bytes >> 10));
    check(st.kheap_bytes > 0, "kernel heap in use is reported (KB)", (int64_t)(st.kheap_bytes >> 10));

    /* Growing the heap by 1 MiB shows up in self_bytes */
    int64_t base = sc1(SYS_BRK, 0);
    int64_t top = sc1(SYS_BRK, base + (1 << 20));
    volatile uint8_t *p = (volatile uint8_t *)base;
    for (int i = 0; i < (1 << 20); i += 4096) p[i] = 1;
    nx_mem_stats_t st2;
    sc2(SYS_MEM_STATS, (int64_t)&st2, sizeof(st2));
    check(top >= base + (1 << 20) && st2.self_bytes >= st.self_bytes + (1 << 20),
          "1 MiB of heap counted in self_bytes (KB more)", (int64_t)((st2.self_bytes - st.self_bytes) >> 10));

    /* A larger caller size is fine; the kernel fills only v1 */
    uint8_t big[96];
    for (int i = 0; i < 96; i++) big[i] = 0xAB;
    r = sc2(SYS_MEM_STATS, (int64_t)big, sizeof(big));
    check(r == 0 && ((nx_mem_stats_t *)(void *)big)->size == 80 && big[80] == 0xAB,
          "size 96 fills 80 bytes and leaves the rest", r);
}

static void test_set_class(void) {
    int64_t self = sc0(SYS_GETPID);
    int64_t parent = sc0(SYS_GETPPID);

    int64_t r = sc2(SYS_PROC_SET_CLASS, self, 0);
    check(r == -E_INVAL, "class 0 is -EINVAL", r);
    r = sc2(SYS_PROC_SET_CLASS, self, 3);
    check(r == -E_INVAL, "class 3 is -EINVAL", r);
    r = sc2(SYS_PROC_SET_CLASS, 0, NX_CLASS_BACKGROUND);
    check(r == -E_ACCES, "the idle process (pid 0) is -EACCES", r);
    r = sc2(SYS_PROC_SET_CLASS, 4000, NX_CLASS_BACKGROUND);
    check(r == -E_SRCH, "pid 4000 is -ESRCH", r);
    r = sc2(SYS_PROC_SET_CLASS, 250, NX_CLASS_BACKGROUND);
    check(r == -E_SRCH, "an unused pid is -ESRCH", r);
    r = sc2(SYS_PROC_SET_CLASS, parent, NX_CLASS_BACKGROUND);
    check(r == -E_ACCES, "an app cannot set the desktop's class", r);
    r = sc2(SYS_PROC_SET_CLASS, self, NX_CLASS_FOREGROUND);
    check(r == -E_ACCES, "an app cannot raise itself to FOREGROUND", r);

    /* Another app: neither direction is allowed */
    static const char *argv[] = { "/apps/classtest", "spin", "bg", 0 };
    int64_t child = sc3(SYS_SPAWN, (int64_t)"/apps/classtest", (int64_t)argv, SPAWN_WAITABLE);
    check(child > 0, "spawn a second app", child);
    if (child > 0) {
        r = sc2(SYS_PROC_SET_CLASS, child, NX_CLASS_BACKGROUND);
        check(r == -E_ACCES, "an app cannot send another app to BACKGROUND", r);
        r = sc2(SYS_PROC_SET_CLASS, child, NX_CLASS_FOREGROUND);
        check(r == -E_ACCES, "an app cannot raise another app", r);
        int status = 0;
        sc2(SYS_WAIT, child, (int64_t)&status);
        check(status > 0, "the child set itself to BACKGROUND and ran (kloops)", status);
    }

    r = sc2(SYS_PROC_SET_CLASS, self, NX_CLASS_BACKGROUND);
    check(r == 0, "an app may send itself to BACKGROUND", r);
    r = sc2(SYS_PROC_SET_CLASS, self, NX_CLASS_BACKGROUND);
    check(r == 0, "repeating BACKGROUND is fine", r);
    r = sc2(SYS_PROC_SET_CLASS, self, NX_CLASS_FOREGROUND);
    check(r == -E_ACCES, "and cannot come back by itself", r);
}

static void test_pressure(void) {
    int64_t r = sc1(SYS_MEM_PRESSURE_OPEN, 1);
    check(r == -E_INVAL, "mem.pressure_open with flags 1 is -EINVAL", r);

    int64_t h = sc1(SYS_MEM_PRESSURE_OPEN, 0);
    check(h > 0, "mem.pressure_open returns a handle", h);
    if (h <= 0) return;

    nx_wait_item_t it = { (nx_handle_t)h, NX_SIGNAL_READABLE, 0, 0 };
    r = sc3(SYS_OBJ_WAIT, (int64_t)&it, 1, 0);
    check(r == -E_TIMEDOUT, "a new handle is not READABLE (obj.wait timeout 0)", r);
    it.signals = NX_SIGNAL_WRITABLE;
    r = sc3(SYS_OBJ_WAIT, (int64_t)&it, 1, 0);
    check(r == -E_TIMEDOUT && it.observed == 0, "never WRITABLE", r);

    nx_mem_stats_t st;
    sc2(SYS_MEM_STATS, (int64_t)&st, sizeof(st));
    r = sc1(SYS_MEM_PRESSURE_READ, h);
    check(r >= 0 && (uint32_t)r == st.level, "mem.pressure_read matches mem.stats level", r);

    r = sc1(SYS_MEM_PRESSURE_READ, 0);
    check(r == -E_BADF, "mem.pressure_read of handle 0 is -EBADF", r);
    r = sc1(SYS_MEM_PRESSURE_READ, 0x12345);
    check(r == -E_BADF, "mem.pressure_read of a bogus handle is -EBADF", r);
    int64_t shm = sc2(SYS_SHM_CREATE, 4096, 0);
    r = sc1(SYS_MEM_PRESSURE_READ, shm);
    check(r == -E_INVAL, "mem.pressure_read of a shm handle is -EINVAL", r);
    sc1(SYS_HANDLE_CLOSE, shm);

    int64_t d = sc2(SYS_HANDLE_DUP, h, NX_RIGHT_READ);
    check(d > 0 && d != h, "handle.dup(READ) of a pressure handle", d);
    r = sc1(SYS_MEM_PRESSURE_READ, d);
    check(r >= 0, "the duplicate reads the level", r);
    r = sc2(SYS_HANDLE_DUP, h, NX_RIGHT_WRITE);
    check(r < 0, "handle.dup cannot add WRITE", r);
    check(sc1(SYS_HANDLE_CLOSE, d) == 0, "close the duplicate", 0);
    check(sc1(SYS_HANDLE_CLOSE, h) == 0, "close the pressure handle", 0);
    r = sc1(SYS_MEM_PRESSURE_READ, h);
    check(r == -E_BADF, "a closed pressure handle is -EBADF", r);

    /* The handle table limit applies */
    int64_t hs[NX_HANDLE_MAX];
    int n = 0;
    while (n < NX_HANDLE_MAX) {
        int64_t x = sc1(SYS_MEM_PRESSURE_OPEN, 0);
        if (x < 0) { r = x; break; }
        hs[n++] = x;
    }
    check(n == NX_HANDLE_MAX || r == -E_MFILE, "pressure handles stop at -EMFILE (opened)", n);
    int64_t extra = sc1(SYS_MEM_PRESSURE_OPEN, 0);
    check(extra == -E_MFILE, "one more is -EMFILE", extra);
    int closed = 0;
    for (int i = 0; i < n; i++) closed += (sc1(SYS_HANDLE_CLOSE, hs[i]) == 0);
    check(closed == n, "all pressure handles closed", closed);
}

static void test_share(void) {
    static const char *fg[] = { "/apps/classtest", "spin", "fg", 0 };
    static const char *bg[] = { "/apps/classtest", "spin", "bg", 0 };
    int64_t pf = sc3(SYS_SPAWN, (int64_t)"/apps/classtest", (int64_t)fg, SPAWN_WAITABLE);
    int64_t pb1 = sc3(SYS_SPAWN, (int64_t)"/apps/classtest", (int64_t)bg, SPAWN_WAITABLE);
    int64_t pb2 = sc3(SYS_SPAWN, (int64_t)"/apps/classtest", (int64_t)bg, SPAWN_WAITABLE);
    check(pf > 0 && pb1 > 0 && pb2 > 0, "spawn 1 FOREGROUND and 2 BACKGROUND spinners", pf);
    if (pf <= 0 || pb1 <= 0 || pb2 <= 0) return;

    int sf = 0, s1 = 0, s2 = 0;
    sc2(SYS_WAIT, pf, (int64_t)&sf);
    sc2(SYS_WAIT, pb1, (int64_t)&s1);
    sc2(SYS_WAIT, pb2, (int64_t)&s2);

    out_str("[CLASSTEST] kloops in 3 s: FOREGROUND ");
    out_sdec(sf); out_str(", BACKGROUND "); out_sdec(s1); out_str(" and "); out_sdec(s2);
    out_str("\n");
    flush();
    int64_t bg_avg = ((int64_t)s1 + (int64_t)s2) / 2;
    check(s1 > 0 && s2 > 0, "BACKGROUND spinners were not starved (fewest kloops)", s1 < s2 ? s1 : s2);
    /* Expected 4x (FOREGROUND gets 2 of 3 app turns, shared by one). */
    int64_t ratio10 = bg_avg > 0 ? (int64_t)sf * 10 / bg_avg : 0;
    check(ratio10 >= 25 && ratio10 <= 60, "FOREGROUND:BACKGROUND share about 4:1 (x10)", ratio10);
}

void _start(long argc, char **argv) {
    if (argc > 2 && argv && streq(argv[1], "spin")) spin_main(argv[2]);

    out_str("[CLASSTEST] PID ");
    out_sdec(sc0(SYS_GETPID));
    out_str(" started\n");
    flush();

    test_abi();
    test_stats();
    test_pressure();
    test_share();
    test_set_class();       /* last: it moves this process to BACKGROUND */

    out_str(g_failed ? "[CLASSTEST] RESULT: FAIL\n" : "[CLASSTEST] RESULT: PASS\n");
    flush();
    finish(g_failed ? 1 : 0);
}
