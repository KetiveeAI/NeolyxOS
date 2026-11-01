/*
 * NeolyxOS memstress - K0b memory reserve test
 *
 * Fills physical memory from ordinary (NORMAL class) processes and checks
 * that the kernel refuses them before free memory drops into the reserve,
 * while the desktop keeps running.
 *
 *   /apps/memstress          parent: spawns up to MS_CHILDREN copies of
 *                            itself with argv[1] = "child", waits for all,
 *                            prints a summary.
 *   /apps/memstress child    grows its heap 1 MiB at a time (writing every
 *                            page) until brk is refused or the 64 MiB heap
 *                            limit is hit, holds the memory for MS_HOLD_MS
 *                            so the desktop can be checked under pressure,
 *                            then exits.
 *
 * The kernel logs each refusal with the free memory and the reserve
 * ("[MEM] user mapping refused: ..."), which is what the test checks.
 *
 * K1: the parent also listens on a memory pressure handle. It must see
 * WARN and CRITICAL while the children fill memory, NORMAL again after
 * they exit, and mem.stats must count the refusals.
 * Children exit with 0 when stopped by a refusal, 1 when stopped by the
 * heap limit, 2 on a memory content error.
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>
#include "../../../kernel/include/core/nx_ipc.h"
#include "../../../kernel/include/core/nx_mem.h"

/* ============ Syscalls (numbers from kernel/include/core/syscall.h) ============ */

#define SYS_EXIT        1
#define SYS_SLEEP       2
#define SYS_TIME        3
#define SYS_GETPID      5
#define SYS_WAIT        9
#define SYS_BRK         10
#define SYS_SPAWN       13
#define SYS_DEBUG_PRINT 127
#define SYS_OBJ_WAIT    147
#define SYS_HANDLE_CLOSE 140
#define SYS_MEM_PRESSURE_OPEN 154
#define SYS_MEM_PRESSURE_READ 155
#define SYS_MEM_STATS   156

#define SPAWN_WAITABLE  0x1

#define MS_CHILDREN     40              /* 40 x 64 MiB covers a 2 GiB guest */
#define MS_STEP         (1024 * 1024)   /* heap growth per brk call */
#define MS_HEAP_LIMIT   (64ULL * 1024 * 1024)
#define MS_HOLD_MS      6000
#define MS_PAGE         4096

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

static char g_line[160];
static int g_len;

static void out_str(const char *s) {
    while (s && *s && g_len < (int)sizeof(g_line) - 1) g_line[g_len++] = *s++;
}

static void out_dec(int64_t v) {
    char buf[21];
    int i = 20;
    uint64_t u = (v < 0) ? (uint64_t)(-v) : (uint64_t)v;
    buf[i] = '\0';
    if (u == 0) buf[--i] = '0';
    while (u > 0 && i > 0) { buf[--i] = (char)('0' + u % 10); u /= 10; }
    if (v < 0) out_str("-");
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

static int str_eq(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

/* ============ Child: take memory until refused ============ */

static void run_child(void) {
    int64_t pid = sc0(SYS_GETPID);
    int64_t base = sc1(SYS_BRK, 0);
    if (base <= 0) {
        out_str("[MEMSTRESS] child PID "); out_dec(pid);
        out_str(": FAIL: no heap ("); out_dec(base); out_str(")\n");
        flush();
        finish(2);
    }

    uint64_t got = 0;
    int refused = 0;
    while (got + MS_STEP <= MS_HEAP_LIMIT) {
        int64_t want = base + (int64_t)(got + MS_STEP);
        int64_t r = sc1(SYS_BRK, want);
        if (r < want) { refused = 1; break; }
        /* Touch every page so the memory is really in use, tagged by pid */
        volatile uint64_t *p = (volatile uint64_t *)(base + (int64_t)got);
        for (uint64_t off = 0; off < MS_STEP; off += MS_PAGE) {
            p[off / 8] = ((uint64_t)pid << 32) | (got + off);
        }
        got += MS_STEP;
    }

    /* Check what we hold still reads back */
    int bad = 0;
    for (uint64_t off = 0; off < got; off += MS_PAGE) {
        volatile uint64_t *p = (volatile uint64_t *)(base + (int64_t)off);
        if (*p != (((uint64_t)pid << 32) | off)) { bad = 1; break; }
    }

    out_str("[MEMSTRESS] child PID "); out_dec(pid);
    out_str(": holding "); out_dec((int64_t)(got / MS_STEP));
    out_str(" MB, stopped by ");
    out_str(refused ? "refusal" : "heap limit");
    out_str(bad ? ", FAIL: content mismatch\n" : "\n");
    flush();

    sc1(SYS_SLEEP, MS_HOLD_MS);
    finish(bad ? 2 : (refused ? 0 : 1));
}

/* ============ Parent: pressure listener (K1) ============ */

static int64_t g_ph = -1;           /* pressure handle, or < 0 */
static int g_seen[3];               /* levels seen */
static int g_last_level = -1;

static const char *level_name(int l) {
    return l == 0 ? "NORMAL" : l == 1 ? "WARN" : l == 2 ? "CRITICAL" : "?";
}

/* Wait up to ms for a level change; record and print it. Returns the
 * level read, or -1 when nothing changed. */
static int poll_pressure(uint64_t ms) {
    if (g_ph < 0) return -1;
    nx_wait_item_t it = { (nx_handle_t)g_ph, NX_SIGNAL_READABLE, 0, 0 };
    int64_t r = sc3(SYS_OBJ_WAIT, (int64_t)&it, 1, (int64_t)ms);
    if (r <= 0) return -1;
    int64_t level = sc1(SYS_MEM_PRESSURE_READ, g_ph);
    if (level < 0 || level > 2) return -1;
    g_seen[level] = 1;
    if (level != g_last_level) {
        nx_mem_stats_t st;
        sc2(SYS_MEM_STATS, (int64_t)&st, sizeof(st));
        out_str("[MEMSTRESS] pressure "); out_str(level_name((int)level));
        out_str(": "); out_dec((int64_t)(st.free_bytes >> 20));
        out_str(" MB free, "); out_dec((int64_t)st.refusals); out_str(" refusals so far\n");
        flush();
        g_last_level = (int)level;
    }
    return (int)level;
}

/* ============ Parent: spawn children and wait ============ */

void _start(long argc, char **argv) {
    if (argc > 1 && argv && str_eq(argv[1], "child")) run_child();

    int64_t pid = sc0(SYS_GETPID);
    out_str("[MEMSTRESS] PID "); out_dec(pid);
    out_str(" starting up to "); out_dec(MS_CHILDREN);
    out_str(" children of 64 MB each\n");
    flush();

    nx_mem_stats_t st0;
    int have_stats = (sc2(SYS_MEM_STATS, (int64_t)&st0, sizeof(st0)) == 0);
    g_ph = sc1(SYS_MEM_PRESSURE_OPEN, 0);
    if (g_ph < 0 || !have_stats) {
        out_str("[MEMSTRESS] no memory pressure API (ABI < 1.3): checking refusals only\n");
        flush();
        g_ph = -1;
    } else {
        g_last_level = (int)sc1(SYS_MEM_PRESSURE_READ, g_ph);
        out_str("[MEMSTRESS] start: "); out_dec((int64_t)(st0.free_bytes >> 20));
        out_str(" MB free, reserve "); out_dec((int64_t)(st0.reserve_bytes >> 20));
        out_str(" MB, level "); out_str(level_name(g_last_level)); out_str("\n");
        flush();
    }

    static const char *child_argv[] = { "/apps/memstress", "child", 0 };
    int32_t kids[MS_CHILDREN];
    int nkids = 0, spawn_refused = 0;
    for (int i = 0; i < MS_CHILDREN; i++) {
        int64_t c = sc3(SYS_SPAWN, (int64_t)"/apps/memstress",
                        (int64_t)child_argv, SPAWN_WAITABLE);
        if (c <= 0) {
            out_str("[MEMSTRESS] spawn "); out_dec(i);
            out_str(" refused ("); out_dec(c); out_str(")\n");
            flush();
            spawn_refused = 1;
            break;
        }
        kids[nkids++] = (int32_t)c;
        /* Let the child fill up before starting the next, so the order of
         * refusals is easy to read in the log; watch the pressure level */
        if (g_ph >= 0) {
            int64_t until = sc0(SYS_TIME) + 150;
            while (sc0(SYS_TIME) < until) poll_pressure(50);
        } else {
            sc1(SYS_SLEEP, 150);
        }
    }

    int refused = 0, capped = 0, failed = 0;
    for (int i = 0; i < nkids; i++) {
        int status = -1;
        int64_t w = sc2(SYS_WAIT, kids[i], (int64_t)&status);
        if (w < 0) { failed++; continue; }
        if (status == 0) refused++;
        else if (status == 1) capped++;
        else failed++;
    }

    /* Memory is back: the level must return to NORMAL */
    int pressure_ok = 1;
    if (g_ph >= 0) {
        for (int i = 0; i < 30 && g_last_level != 0; i++) poll_pressure(100);
        nx_mem_stats_t st1;
        sc2(SYS_MEM_STATS, (int64_t)&st1, sizeof(st1));
        uint64_t new_refusals = st1.refusals - st0.refusals;
        pressure_ok = g_seen[1] && g_seen[2] && g_last_level == 0 && new_refusals > 0;
        out_str("[MEMSTRESS] pressure seen: WARN "); out_str(g_seen[1] ? "yes" : "no");
        out_str(", CRITICAL "); out_str(g_seen[2] ? "yes" : "no");
        out_str(", back to "); out_str(level_name(g_last_level));
        out_str("; mem.stats refusals +"); out_dec((int64_t)new_refusals);
        out_str(pressure_ok ? "\n" : " (FAIL)\n");
        flush();
        sc1(SYS_HANDLE_CLOSE, g_ph);
        if (!pressure_ok) failed++;
    }

    out_str("[MEMSTRESS] done: "); out_dec(nkids);
    out_str(" children, "); out_dec(capped);
    out_str(" reached 64 MB, "); out_dec(refused);
    out_str(" refused, "); out_dec(failed);
    out_str(" failed");
    out_str(spawn_refused ? ", spawn refused\n" : "\n");
    flush();
    out_str((failed == 0 && (refused > 0 || spawn_refused)) ? "[MEMSTRESS] PASS\n"
            : (failed == 0 ? "[MEMSTRESS] memory never ran low (more RAM than children)\n"
                           : "[MEMSTRESS] FAIL\n"));
    flush();
    finish(failed ? 1 : 0);
}
